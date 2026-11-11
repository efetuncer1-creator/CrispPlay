// Instant replay: a bounded ring of encoded packets (video + audio).
//
// Pure C++ on purpose (no FFmpeg, D3D or Qt) so the eviction and cut rules are
// unit-tested without a GPU. Timestamps are microseconds on one shared clock.
// Packet bytes are shared and immutable: snapshot() copies pointers under the
// lock and the file is written from the snapshot without holding it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace crispplay::replay {

enum class Stream : uint8_t { Video, Audio };

struct Packet {
    Stream stream = Stream::Video;
    int64_t ptsUs = 0;
    int64_t durationUs = 0;   // audio: one frame; video: filled in by snapshot()
    bool key = false;
    std::shared_ptr<const std::vector<uint8_t>> data;
    size_t size() const { return data ? data->size() : 0; }
};

struct RingLimits {
    int64_t durationUs = 30'000'000;       // keep at least this much before the newest video packet
    size_t maxBytes = size_t(512) << 20;   // hard cap; may be exceeded by at most one GOP
};

struct RingStats {
    size_t bytes = 0, videoPackets = 0, audioPackets = 0, keyframes = 0;
    int64_t spanUs = 0;                    // newest video end - oldest video start
    uint64_t byteEvictions = 0;            // GOPs dropped because of maxBytes
    uint64_t rejected = 0;                 // packets refused (video before the first keyframe, empty)
};

// A cut ready for muxing: starts on a video keyframe at time 0, packets in
// timestamp order (video first on a tie).
struct Clip {
    std::vector<Packet> packets;
    int64_t originUs = 0;                  // clock time of the first video packet
    int64_t durationUs = 0;                // end of the last video packet
    size_t bytes = 0, videoPackets = 0, audioPackets = 0;
    bool empty() const { return videoPackets == 0; }
};

class PacketRing {
public:
    explicit PacketRing(RingLimits limits = {}) : limits_(limits) {}

    void setLimits(RingLimits limits) {
        std::lock_guard lock(m_);
        limits_ = limits;
        evict();
    }
    RingLimits limits() const { std::lock_guard lock(m_); return limits_; }

    void clear() {
        std::lock_guard lock(m_);
        video_.clear(); audio_.clear(); bytes_ = 0; keyframes_ = 0;
    }

    // Any thread. Video must arrive in presentation order (no B-frames).
    void push(Packet packet) {
        std::lock_guard lock(m_);
        if (!packet.data || packet.data->empty()) { ++rejected_; return; }
        if (packet.stream == Stream::Video) {
            // Nothing can be cut from before the first keyframe.
            if (video_.empty() && !packet.key) { ++rejected_; return; }
            if (!video_.empty() && packet.ptsUs <= video_.back().ptsUs) packet.ptsUs = video_.back().ptsUs + 1;
            if (packet.key) ++keyframes_;
            bytes_ += packet.size();
            video_.push_back(std::move(packet));
        } else {
            if (!audio_.empty() && packet.ptsUs <= audio_.back().ptsUs) { ++rejected_; return; }
            bytes_ += packet.size();
            audio_.push_back(std::move(packet));
        }
        evict();
    }

    RingStats stats() const {
        std::lock_guard lock(m_);
        RingStats out;
        out.bytes = bytes_; out.videoPackets = video_.size(); out.audioPackets = audio_.size();
        out.keyframes = keyframes_; out.byteEvictions = byteEvictions_; out.rejected = rejected_;
        if (!video_.empty()) out.spanUs = video_.back().ptsUs - video_.front().ptsUs + lastFrameUs();
        return out;
    }

    // The last `durationUs` before the newest video packet (0: the configured
    // duration), starting at the keyframe at or before that point. Shorter
    // when the ring holds less.
    Clip snapshot(int64_t durationUs = 0) const {
        std::lock_guard lock(m_);
        Clip clip;
        if (video_.empty()) return clip;
        if (durationUs <= 0) durationUs = limits_.durationUs;
        const int64_t endUs = video_.back().ptsUs + lastFrameUs();
        const int64_t targetUs = endUs - durationUs;
        size_t start = 0;   // the front is always a keyframe
        for (size_t i = 0; i < video_.size(); ++i) {
            if (video_[i].ptsUs > targetUs) break;
            if (video_[i].key) start = i;
        }
        const int64_t origin = video_[start].ptsUs;
        std::vector<Packet> video(video_.begin() + ptrdiff_t(start), video_.end());
        for (size_t i = 0; i < video.size(); ++i) {
            video[i].durationUs = i + 1 < video.size() ? video[i + 1].ptsUs - video[i].ptsUs : lastFrameUs();
            video[i].ptsUs -= origin;
        }
        std::vector<Packet> audio;
        for (const auto& packet : audio_) {
            if (packet.ptsUs < origin || packet.ptsUs >= endUs) continue;
            audio.push_back(packet);
            audio.back().ptsUs -= origin;
        }
        clip.originUs = origin; clip.durationUs = endUs - origin;
        clip.videoPackets = video.size(); clip.audioPackets = audio.size();
        clip.packets.reserve(video.size() + audio.size());
        size_t v = 0, a = 0;
        while (v < video.size() || a < audio.size()) {
            const bool takeVideo = a >= audio.size() || (v < video.size() && video[v].ptsUs <= audio[a].ptsUs);
            auto& packet = takeVideo ? video[v++] : audio[a++];
            clip.bytes += packet.size();
            clip.packets.push_back(std::move(packet));
        }
        return clip;
    }

private:
    // Duration assumed for the newest frame: the previous interval, 1/60 s when unknown.
    int64_t lastFrameUs() const {
        if (video_.size() < 2) return 16'667;
        return std::clamp<int64_t>(video_.back().ptsUs - video_[video_.size() - 2].ptsUs, 1, 1'000'000);
    }
    // Index of the second keyframe, 0 when there is only one GOP.
    size_t secondKey() const {
        for (size_t i = 1; i < video_.size(); ++i) if (video_[i].key) return i;
        return 0;
    }
    void dropFrontGop(size_t next) {
        for (size_t i = 0; i < next; ++i) { bytes_ -= video_.front().size(); video_.pop_front(); }
        --keyframes_;
    }
    void evict() {
        // Duration: the oldest GOP goes once the next keyframe alone covers the window.
        while (keyframes_ >= 2) {
            const size_t next = secondKey();
            if (!next || video_[next].ptsUs > video_.back().ptsUs + lastFrameUs() - limits_.durationUs) break;
            dropFrontGop(next);
        }
        // Bytes: oldest GOPs go regardless of the window.
        while (bytes_ > limits_.maxBytes && keyframes_ >= 2) {
            const size_t next = secondKey();
            if (!next) break;
            dropFrontGop(next);
            ++byteEvictions_;
        }
        // Audio older than the oldest picture cannot be used; without any
        // picture keep one window of it.
        const int64_t floorUs = !video_.empty() ? video_.front().ptsUs
                              : !audio_.empty() ? audio_.back().ptsUs - limits_.durationUs : 0;
        while (!audio_.empty() && audio_.front().ptsUs + audio_.front().durationUs <= floorUs) {
            bytes_ -= audio_.front().size();
            audio_.pop_front();
        }
    }

    mutable std::mutex m_;
    RingLimits limits_;
    std::deque<Packet> video_, audio_;
    size_t bytes_ = 0, keyframes_ = 0;
    uint64_t byteEvictions_ = 0, rejected_ = 0;
};

// Where each audio frame of a clip lands on the fixed frame grid of the MP4
// track. The ring holds audio sparsely (nothing while paused or silent), so
// gaps are filled with silent frames; `source` is an index into the input or
// -1 for silence. Positions are in samples.
struct AudioSlot {
    int source = -1;
    int64_t ptsSamples = 0;
};

inline std::vector<AudioSlot> layoutAudio(const std::vector<int64_t>& ptsUs, int64_t clipDurationUs,
                                          int sampleRate = 48000, int frameSamples = 1024) {
    std::vector<AudioSlot> out;
    if (ptsUs.empty() || sampleRate <= 0 || frameSamples <= 0) return out;
    const auto samples = [sampleRate](int64_t us) { return (us * sampleRate + (us >= 0 ? 500'000 : -500'000)) / 1'000'000; };
    const int64_t end = samples(clipDurationUs);
    int64_t pos = 0;
    for (size_t i = 0; i < ptsUs.size(); ++i) {
        const int64_t at = samples(ptsUs[i]);
        if (at < pos - frameSamples / 2) continue;                 // overlaps what is already placed
        if (at >= end) break;
        for (int64_t gap = (at - pos + frameSamples / 2) / frameSamples; gap > 0; --gap) {
            out.push_back({-1, pos});
            pos += frameSamples;
        }
        out.push_back({int(i), pos});
        pos += frameSamples;
    }
    while (pos + frameSamples / 2 <= end) {
        out.push_back({-1, pos});
        pos += frameSamples;
    }
    return out;
}

} // namespace crispplay::replay
