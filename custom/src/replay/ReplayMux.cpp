// Instant replay: writes a ring snapshot as an MP4. Saver thread only.
#include "crispplay/replay/ReplayService.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

namespace crispplay::replay {
namespace {

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::wstring avError(const wchar_t* what, int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof(buffer));
    std::wstring out = what;
    out += L": ";
    for (const char* c = buffer; *c; ++c) out += wchar_t(*c);
    return out;
}
bool setExtradata(AVCodecParameters* par, const std::vector<uint8_t>& bytes) {
    par->extradata = static_cast<uint8_t*>(av_mallocz(bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!par->extradata) return false;
    std::memcpy(par->extradata, bytes.data(), bytes.size());
    par->extradata_size = int(bytes.size());
    return true;
}

struct Mux {
    AVFormatContext* ctx = nullptr;
    AVPacket* packet = nullptr;
    ~Mux() {
        av_packet_free(&packet);
        if (ctx) {
            if (ctx->pb) avio_closep(&ctx->pb);
            avformat_free_context(ctx);
        }
    }
};

} // namespace

bool writeClip(const std::wstring& path, const Clip& clip, const VideoInfo& video, const AudioInfo* audio, std::wstring& error) {
    error.clear();
    if (clip.empty() || video.headers.empty()) { error = L"empty clip"; return false; }
    const std::wstring partial = path + L".part";
    std::error_code code;
    auto fail = [&](std::wstring reason) {
        error = std::move(reason);
        std::filesystem::remove(partial, code);
        return false;
    };

    // The audio frames of the clip on the track's fixed frame grid; gaps
    // (pause, silence, the encoder's tail) become silent frames.
    std::vector<const Packet*> audioPackets;
    std::vector<int64_t> audioPts;
    for (const auto& packet : clip.packets)
        if (packet.stream == Stream::Audio) { audioPackets.push_back(&packet); audioPts.push_back(packet.ptsUs); }
    std::vector<AudioSlot> slots;
    if (audio && audio->valid() && !audioPackets.empty())
        slots = layoutAudio(audioPts, clip.durationUs, audio->sampleRate, audio->frameSamples);

    Mux mux;
    if (avformat_alloc_output_context2(&mux.ctx, nullptr, "mp4", nullptr) < 0 || !mux.ctx) return fail(L"cannot create the MP4 muxer");
    mux.packet = av_packet_alloc();
    if (!mux.packet) return fail(L"out of memory");

    AVStream* videoStream = avformat_new_stream(mux.ctx, nullptr);
    if (!videoStream) return fail(L"cannot create the video stream");
    videoStream->time_base = AVRational{1, 1000000};
    videoStream->avg_frame_rate = AVRational{int(std::max(1u, video.fps)), 1};
    {
        auto* par = videoStream->codecpar;
        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->codec_id = video.hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        par->width = int(video.width); par->height = int(video.height);
        par->format = video.hdr ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
        par->color_range = AVCOL_RANGE_MPEG;
        par->color_space = video.hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
        par->color_primaries = video.hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
        par->color_trc = video.hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
        if (video.hdr) par->profile = AV_PROFILE_HEVC_MAIN_10;
        if (!setExtradata(par, video.headers)) return fail(L"out of memory");
    }
    AVStream* audioStream = nullptr;
    if (!slots.empty()) {
        audioStream = avformat_new_stream(mux.ctx, nullptr);
        if (!audioStream) return fail(L"cannot create the audio stream");
        audioStream->time_base = AVRational{1, audio->sampleRate};
        auto* par = audioStream->codecpar;
        par->codec_type = AVMEDIA_TYPE_AUDIO;
        par->codec_id = AV_CODEC_ID_AAC;
        par->sample_rate = audio->sampleRate;
        par->frame_size = audio->frameSamples;
        par->format = AV_SAMPLE_FMT_FLTP;
        par->bit_rate = 192000;
        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&par->ch_layout, &stereo);
        if (!setExtradata(par, audio->extradata)) return fail(L"out of memory");
    }

    int rc = avio_open(&mux.ctx->pb, utf8(partial).c_str(), AVIO_FLAG_WRITE);
    if (rc < 0) return fail(avError(L"cannot create the file", rc));
    rc = avformat_write_header(mux.ctx, nullptr);
    if (rc < 0) return fail(avError(L"cannot write the file header", rc));

    auto write = [&](AVStream* stream, const std::vector<uint8_t>& bytes, int64_t pts, int64_t duration, AVRational base, bool key) {
        AVPacket* out = mux.packet;
        av_packet_unref(out);
        // Not reference counted: the muxer copies what it queues.
        out->data = const_cast<uint8_t*>(bytes.data());
        out->size = int(bytes.size());
        out->stream_index = stream->index;
        out->pts = out->dts = pts;
        out->duration = std::max<int64_t>(1, duration);
        if (key) out->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(out, base, stream->time_base);
        return av_interleaved_write_frame(mux.ctx, out);
    };

    size_t nextSlot = 0;
    const AVRational micro{1, 1000000};
    auto writeAudioUntil = [&](int64_t limitUs) {
        const AVRational base{1, audio ? audio->sampleRate : 48000};
        while (nextSlot < slots.size()) {
            const auto& slot = slots[nextSlot];
            if (av_compare_ts(slot.ptsSamples, base, limitUs, micro) > 0) break;
            const auto& bytes = slot.source >= 0 ? *audioPackets[size_t(slot.source)]->data : *audio->silence;
            const int result = write(audioStream, bytes, slot.ptsSamples, audio->frameSamples, base, true);
            if (result < 0) return result;
            ++nextSlot;
        }
        return 0;
    };
    int64_t lastVideoPts = -1;
    for (const auto& packet : clip.packets) {
        if (packet.stream != Stream::Video) continue;
        if (audioStream && (rc = writeAudioUntil(packet.ptsUs)) < 0) return fail(avError(L"cannot write audio", rc));
        const int64_t pts = std::max(packet.ptsUs, lastVideoPts + 1);
        lastVideoPts = pts;
        rc = write(videoStream, *packet.data, pts, packet.durationUs, micro, packet.key);
        if (rc < 0) return fail(avError(L"cannot write video", rc));
    }
    if (audioStream && (rc = writeAudioUntil(INT64_MAX / 4)) < 0) return fail(avError(L"cannot write audio", rc));
    rc = av_write_trailer(mux.ctx);
    if (rc < 0) return fail(avError(L"cannot finish the file", rc));
    rc = avio_closep(&mux.ctx->pb);
    if (rc < 0) return fail(avError(L"cannot finish the file", rc));

    std::filesystem::rename(partial, path, code);
    if (code) return fail(L"cannot rename the finished file");
    return true;
}

} // namespace crispplay::replay
