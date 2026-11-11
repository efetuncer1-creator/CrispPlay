// crispplay_replay_clip_check <file.mp4> [--min-seconds S] [--max-seconds S] [--audio]
// ffprobe-like validation of a saved instant replay through the FFmpeg
// libraries: container, streams, keyframe-first, monotonic timestamps, and a
// full software decode of both tracks. Exit 0 when everything holds.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
struct Track {
    int index = -1;
    AVCodecContext* decoder = nullptr;
    int64_t packets = 0, frames = 0, lastDts = INT64_MIN, bytes = 0;
    bool firstKey = false, monotonic = true;
    double firstPts = -1, endPts = 0;
};
bool openDecoder(AVFormatContext* format, Track& track) {
    const AVStream* stream = format->streams[track.index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    track.decoder = avcodec_alloc_context3(codec);
    return track.decoder && avcodec_parameters_to_context(track.decoder, stream->codecpar) >= 0 && avcodec_open2(track.decoder, codec, nullptr) >= 0;
}
void drain(Track& track, AVFrame* frame) {
    while (avcodec_receive_frame(track.decoder, frame) >= 0) { ++track.frames; av_frame_unref(frame); }
}
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <file.mp4> [--min-seconds S] [--max-seconds S] [--audio]\n", argv[0]); return 2; }
    double minSeconds = 0, maxSeconds = 1e9;
    bool wantAudio = false;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--min-seconds") && i + 1 < argc) minSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--audio")) wantAudio = true;
    }
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(format, nullptr) < 0) {
        std::printf("FAIL cannot open %s\n", argv[1]);
        return 1;
    }
    check(std::strstr(format->iformat->name, "mp4") != nullptr, "container is MP4");
    Track video, audio;
    video.index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio.index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    check(video.index >= 0, "has a video stream");
    if (video.index < 0) return 1;
    const AVCodecParameters* vpar = format->streams[video.index]->codecpar;
    check(vpar->codec_id == AV_CODEC_ID_H264 || vpar->codec_id == AV_CODEC_ID_HEVC, "video is H.264 or HEVC");
    check(vpar->width >= 16 && vpar->height >= 16, "video has a size");
    check(vpar->extradata_size > 0, "video has parameter sets (avcC/hvcC)");
    check(openDecoder(format, video), "video decoder opens");
    if (audio.index >= 0) {
        const AVCodecParameters* apar = format->streams[audio.index]->codecpar;
        check(apar->codec_id == AV_CODEC_ID_AAC && apar->sample_rate == 48000 && apar->ch_layout.nb_channels == 2, "audio is AAC 48 kHz stereo");
        check(openDecoder(format, audio), "audio decoder opens");
    }
    if (wantAudio) check(audio.index >= 0, "has an audio stream");

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool decodeOk = true;
    while (av_read_frame(format, packet) >= 0) {
        Track* track = packet->stream_index == video.index ? &video : packet->stream_index == audio.index ? &audio : nullptr;
        if (track) {
            const AVStream* stream = format->streams[track->index];
            const double pts = double(packet->pts) * av_q2d(stream->time_base);
            if (!track->packets) { track->firstKey = (packet->flags & AV_PKT_FLAG_KEY) != 0; track->firstPts = pts; }
            if (packet->dts <= track->lastDts) track->monotonic = false;
            track->lastDts = packet->dts;
            track->endPts = pts + double(packet->duration) * av_q2d(stream->time_base);
            ++track->packets; track->bytes += packet->size;
            if (track->decoder) {
                if (avcodec_send_packet(track->decoder, packet) < 0) decodeOk = false;
                drain(*track, frame);
            }
        }
        av_packet_unref(packet);
    }
    for (Track* track : {&video, &audio})
        if (track->decoder) { avcodec_send_packet(track->decoder, nullptr); drain(*track, frame); }

    const double videoSeconds = video.endPts - video.firstPts;
    check(video.packets > 0 && video.firstKey, "first video packet is a keyframe");
    check(std::fabs(video.firstPts) < 0.001, "video starts at 0");
    check(video.monotonic, "video timestamps increase");
    check(decodeOk, "every packet is accepted by its decoder");
    check(video.frames == video.packets, "every video packet decodes to a frame");
    check(videoSeconds >= minSeconds && videoSeconds <= maxSeconds, "video duration within the expected range");
    if (audio.index >= 0) {
        const double audioSeconds = audio.endPts - audio.firstPts;
        check(audio.monotonic, "audio timestamps increase");
        check(audio.frames >= audio.packets - 2 && audio.frames > 0, "audio decodes");
        check(std::fabs(audio.firstPts) < 0.05, "audio starts with the picture");
        check(std::fabs(audioSeconds - videoSeconds) < 0.1, "audio is as long as the picture (within 100 ms)");
        std::printf("audio packets=%lld frames=%lld seconds=%.3f bytes=%lld\n", (long long)audio.packets, (long long)audio.frames, audioSeconds, (long long)audio.bytes);
    }
    std::printf("video codec=%s %dx%d packets=%lld frames=%lld seconds=%.3f bytes=%lld fps=%.2f mbps=%.2f\n",
        avcodec_get_name(vpar->codec_id), vpar->width, vpar->height, (long long)video.packets, (long long)video.frames, videoSeconds,
        (long long)video.bytes, videoSeconds > 0 ? double(video.packets) / videoSeconds : 0.0, videoSeconds > 0 ? double(video.bytes) * 8 / videoSeconds / 1e6 : 0.0);
    std::printf("%s (%d failure%s)\n", failures ? "INVALID" : "VALID", failures, failures == 1 ? "" : "s");
    av_frame_free(&frame); av_packet_free(&packet);
    avcodec_free_context(&video.decoder); avcodec_free_context(&audio.decoder);
    avformat_close_input(&format);
    return failures ? 1 : 0;
}
