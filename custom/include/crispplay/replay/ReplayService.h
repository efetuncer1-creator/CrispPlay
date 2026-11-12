// Instant replay engine side: keeps the last N seconds of the presented
// picture and the played sound as encoded packets and writes them to an MP4
// on request. See custom/docs/REPLAY.md.
//
// Default off. While enabled it runs its own NVENC session next to the live
// recorder (LiveRecorder is untouched): the frame is converted to NV12/P010
// inside the presenter's command list and NVENC waits for it on the GPU, so
// the engine thread never blocks and no frame is read back to the CPU.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "crispplay/present/Hooks.h"
#include "crispplay/replay/Hooks.h"
#include "crispplay/replay/PacketRing.h"

namespace crispplay::replay {

struct Settings {
    bool enabled = false;
    int seconds = 30;              // 15 / 30 / 60 / 120
    bool hevc = false;             // follows the recording codec setting; HDR is always HEVC 10-bit
    bool audio = true;
    uint32_t fpsLimit = 60;        // 0: every presented frame
    uint32_t maxMegabytes = 512;   // ring byte cap
    bool operator==(const Settings&) const = default;
};

enum class SaveError { None, Disabled, Empty, Busy, Directory, DiskSpace, Write };

struct Status {
    bool enabled = false;
    bool capturing = false;        // an encoder session is running
    bool unsupported = false;      // this adapter has no NVENC path
    std::wstring encoder;          // e.g. "NVIDIA NVENC H.264"
    std::wstring error;            // why capturing stopped (English, for the log and a hint)
    uint32_t width = 0, height = 0, fps = 0, bitrateMbps = 0;
    double bufferedSeconds = 0;
    uint64_t bufferedBytes = 0;
    uint64_t frames = 0, dropped = 0;
    bool audio = false;            // audio packets are being buffered
    bool saving = false;
    uint64_t saved = 0, saveFailures = 0;   // +1 per finished / failed save
    std::wstring lastPath;         // the last file written
    double lastSeconds = 0;        // its duration
    SaveError lastError = SaveError::None;
    std::wstring lastErrorDetail;
};

// What the muxer needs besides the packets.
struct VideoInfo {
    uint32_t width = 0, height = 0, fps = 60;
    bool hevc = false, hdr = false;
    std::vector<uint8_t> headers;                         // Annex B parameter sets
};
struct AudioInfo {
    int sampleRate = 48000, frameSamples = 1024;
    std::vector<uint8_t> extradata;                       // AudioSpecificConfig
    std::shared_ptr<const std::vector<uint8_t>> silence;  // one encoded silent frame
    bool valid() const { return !extradata.empty() && silence && !silence->empty(); }
};
// Writes `clip` as a plain MP4 (through "<path>.part", renamed when complete).
// Blocking; called on the saver thread. `audio` may be null.
bool writeClip(const std::wstring& path, const Clip& clip, const VideoInfo& video, const AudioInfo* audio, std::wstring& error);

class Service final : public Backend {
public:
    // Process lifetime; installs itself as the hook backend on first use.
    static Service& instance();

    // Any thread. Turning it off or changing the codec/limits restarts the buffer.
    void configure(const Settings& settings);
    Settings settings() const;
    // Any thread; returns at once. The file is written in the background and
    // reported through status() (saved / saveFailures).
    SaveError save(const std::wstring& directory);
    Status status();
    // Stops capturing and waits for the worker threads, at most timeoutMs.
    void shutdown(uint32_t timeoutMs);

    // Engine thread (present chain tap): the session that owns the device ended.
    void sessionEnded();
    // Backend (audio hooks).
    bool audioWanted() const override;
    std::shared_ptr<veyra::sink::RecordingAudioSink> audioTap(std::shared_ptr<veyra::sink::RecordingAudioSink> inner) override;

    struct Impl;
private:
    Service();
    ~Service() override;
    std::unique_ptr<Impl> p_;
};

} // namespace crispplay::replay
