// Instant replay engine side. The capture and NVENC submission follow
// upstream's LiveRecorder (src/engine/LiveRecorder.cpp, GPL-3) so both obey
// the same GPU slot/fence and NVENC input protocol; the differences are that
// packets go to a RAM ring instead of a file and that only the NVENC path
// exists (the Media Foundation path reads every frame back to the CPU).
#include "crispplay/replay/ReplayService.h"

#include "veyra/Log.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/sink/NvencLiveEncoder.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace crispplay::replay {
using Microsoft::WRL::ComPtr;
namespace pipeline = veyra::pipeline;
namespace sink = veyra::sink;
using veyra::engine::PresentCapture;

namespace {

constexpr unsigned kSlots = 4;                          // NV12 inputs; NVENC keeps at most 4 outputs pending
constexpr uint32_t kGpuWaitMs = 3000;
constexpr int64_t kKeyIntervalUs = 2'000'000;           // a keyframe at least this often, whatever the frame rate
constexpr int64_t kProbeUs = 500'000;                   // presented frame rate is measured before the encoder opens
constexpr int64_t kRetryUs = 15'000'000;                // after an encoder failure
constexpr int kAudioRate = 48000;
constexpr int64_t kAudioHardGap = kAudioRate * 60 / 1000;    // beyond 60 ms: insert silence or drop
constexpr double kAudioSoftGap = kAudioRate * 5 / 1000.0;    // beyond 5 ms: resample it away
constexpr int64_t kAudioRestartGap = int64_t(kAudioRate) * 2; // beyond 2 s: a new run (the muxer fills the gap)
constexpr int kCompensationWindow = kAudioRate * 4;
constexpr int kCompensationMax = kCompensationWindow / 1000;
constexpr size_t kAudioQueueFrames = size_t(kAudioRate) * 4;

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::wstring fileStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04d-%02d-%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}
// Upstream's automatic recording bitrate: ~0.15 bit per pixel for H.264, HEVC ~2/3 of it.
uint32_t autoBitrateMbps(uint32_t width, uint32_t height, uint32_t fps, bool hevc) {
    const double mbps = double(width) * height * fps * 0.15 * (hevc ? 0.65 : 1.0) / 1e6;
    return uint32_t(std::clamp<long>(std::lround(mbps), hevc ? 5 : 6, hevc ? 120 : 150));
}
// The ring must hold the window plus one GOP inside the byte cap.
uint32_t budgetBitrateMbps(const Settings& s) {
    const double bits = double(s.maxMegabytes) * 1048576.0 * 8.0 * 0.8;
    return uint32_t(std::max(2.0, std::floor(bits / (double(s.seconds) + 2.0) / 1e6)));
}
uint32_t adapterVendor(ID3D12Device* device) {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) return 0;
    DXGI_ADAPTER_DESC1 desc{};
    return SUCCEEDED(adapter->GetDesc1(&desc)) ? desc.VendorId : 0;
}
RingLimits limitsOf(const Settings& s) {
    return {int64_t(s.seconds) * 1'000'000, size_t(s.maxMegabytes) << 20};
}

} // namespace

// One encoder session: the capture slots and conversion (engine thread) and
// the encoder thread. Created by record() on the engine thread.
struct Segment {
    Service::Impl* owner = nullptr;
    uint64_t generation = 0;
    ComPtr<ID3D12Device> device;
    uint32_t width = 0, height = 0, hdrMode = 0, fps = 60, bitrateMbps = 20;
    bool hevc = false;

    // Engine thread.
    pipeline::ComputePass convert;
    pipeline::DescriptorStager stager;
    pipeline::StateTracker states;
    ComPtr<ID3D12Resource> y, uv;
    struct Slot {
        ComPtr<ID3D12Resource> nv12;
        std::atomic<int> state{0};       // 0 free, 1 recorded, 2 queued for the encoder
        ComPtr<ID3D12Fence> fence;       // the presenter's fence; kept alive past the session
        uint64_t value = 0;
        int64_t ptsUs = 0;
    };
    std::array<Slot, kSlots> slots;
    int pending = -1;
    int64_t lastPtsUs = -1;
    std::atomic<uint64_t> frames{0}, dropped{0};

    // Encoder thread.
    std::thread encoderThread;
    std::mutex jobMutex;
    std::condition_variable jobCv;
    std::deque<unsigned> jobs;
    bool jobsClosed = false;
    std::atomic<bool> encoderReady{false}, closing{false}, failed{false}, encoderFinished{false};
    mutable std::mutex textMutex;
    std::wstring error, encoderName;

    ~Segment() { if (encoderThread.joinable()) encoderThread.join(); }

    bool done() const {
        if (!encoderFinished.load() || pending >= 0) return false;
        // A failed encoder may leave submitted capture commands in flight.
        for (const auto& slot : slots)
            if (slot.fence && slot.fence->GetCompletedValue() < slot.value) return false;
        return true;
    }
    std::wstring errorText() const { std::lock_guard lock(textMutex); return error; }
    std::wstring encoderText() const { std::lock_guard lock(textMutex); return encoderName; }
    bool matches(const PresentCapture::Frame& frame) const {
        return frame.width == width && frame.height == height && frame.hdrMode == hdrMode;
    }
    void fail(const std::wstring& reason) {
        { std::lock_guard lock(textMutex); if (error.empty()) error = reason; }
        if (!failed.exchange(true)) veyra::log::error("replay", std::format("encoder session failed: {}", utf8(reason)));
        closeJobs();
    }
    void closeJobs() {
        closing = true;
        { std::lock_guard lock(jobMutex); jobsClosed = true; }
        jobCv.notify_all();
    }
    // Engine thread or any thread holding the service lock: no more frames.
    void finish() {
        closing = true;
        if (pending < 0) closeJobs();
    }

    bool create(std::wstring& reason) {
        std::vector<uint8_t> shader;
        if (!convert.loadShader("RgbToNv12.dxil", shader) || !convert.create(device.Get(), shader, kSlots + 2, 1, 2)) { reason = L"RgbToNv12 shader unavailable"; return false; }
        if (!stager.initialize(device.Get(), kSlots)) { reason = L"descriptor heap allocation failed"; return false; }
        const bool hdr = hdrMode != 0;
        y = pipeline::makeTexture(device.Get(), width, height, hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM, true);
        uv = pipeline::makeTexture(device.Get(), width / 2, height / 2, hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM, true);
        if (!y || !uv) { reason = L"video memory allocation failed"; return false; }
        pipeline::makeUav(device.Get(), y.Get(), hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM, pipeline::cpuHandleOf(convert, kSlots));
        pipeline::makeUav(device.Get(), uv.Get(), hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM, pipeline::cpuHandleOf(convert, kSlots + 1));
        for (auto& slot : slots) {
            slot.nv12 = pipeline::makeTexture(device.Get(), width, height, hdr ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12, false);
            if (!slot.nv12) { reason = L"video memory allocation failed"; return false; }
        }
        encoderThread = std::thread([this] { encoderMain(); });
        return true;
    }

    // Engine thread, inside the presenter's command list. Same commands as
    // LiveRecorder's NVENC branch: one compute pass, two plane copies.
    bool capture(ID3D12GraphicsCommandList* list, const PresentCapture::Frame& frame, int64_t nowUs) {
        if (closing.load() || !encoderReady.load()) return false;
        int index = -1;
        for (unsigned i = 0; i < kSlots; ++i) { int expected = 0; if (slots[i].state.compare_exchange_strong(expected, 1)) { index = int(i); break; } }
        if (index < 0) { ++dropped; return false; }
        auto& slot = slots[size_t(index)];
        int64_t pts = nowUs;
        if (pts <= lastPtsUs) pts = lastPtsUs + 1;
        lastPtsUs = pts; slot.ptsUs = pts;
        // Per-frame SRV through the staging heap (GpuPassUtils.h driver note).
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = frame.format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
        stager.stageSrv(frame.texture, &view, convert.heap.Get(), UINT(index));
        D3D12_RESOURCE_BARRIER source{};
        source.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; source.Transition.pResource = frame.texture;
        source.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        source.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; source.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &source);
        states.transition(list, y.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        states.transition(list, uv.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // reserved.x = HDR mode, reserved.y = output dither step (RgbToNv12.hlsl).
        const float constants[8] = {std::bit_cast<float>(width), std::bit_cast<float>(height), std::bit_cast<float>(hdrMode), frame.ditherStep, 0, 0, 0, 0};
        convert.bind(list, constants, pipeline::gpuHandleOf(convert, UINT(index)).ptr, pipeline::gpuHandleOf(convert, kSlots).ptr);
        list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
        states.uavBarrier(list, y.Get()); states.uavBarrier(list, uv.Get());
        states.transition(list, y.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list, uv.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        std::swap(source.Transition.StateBefore, source.Transition.StateAfter);
        list->ResourceBarrier(1, &source);
        D3D12_RESOURCE_BARRIER target{};
        target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; target.Transition.pResource = slot.nv12.Get();
        target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        target.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; target.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1, &target);
        for (UINT plane = 0; plane < 2; ++plane) {
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = plane ? uv.Get() : y.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; from.SubresourceIndex = 0;
            to.pResource = slot.nv12.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.SubresourceIndex = plane;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
        std::swap(target.Transition.StateBefore, target.Transition.StateAfter);
        list->ResourceBarrier(1, &target);
        pending = index;
        return true;
    }
    void submittedPending(ID3D12Fence* fence, uint64_t value) {
        if (pending < 0) return;
        auto& slot = slots[size_t(pending)]; pending = -1;
        slot.fence = fence; slot.value = value; slot.state = 2;
        { std::lock_guard lock(jobMutex); if (!encoderFinished.load()) jobs.push_back(unsigned(&slot - slots.data())); }
        jobCv.notify_one();
        ++frames;
        if (closing.load()) closeJobs();
    }
    void discardPending() {
        if (pending < 0) return;
        slots[size_t(pending)].state = 0; pending = -1;
        if (closing.load()) closeJobs();
    }
    void releaseSlot(unsigned index) { slots[index].fence.Reset(); slots[index].state = 0; }

    void encoderMain();
};

// Fan-out the presenter calls: the live recorder first, then replay.
struct CaptureFanout final : PresentCapture {
    Service::Impl* owner = nullptr;
    std::atomic<PresentCapture*> inner{nullptr};
    bool innerRecorded = false, ownRecorded = false;   // engine thread
    bool record(ID3D12GraphicsCommandList* list, const Frame& frame) override;
    void submitted(ID3D12Fence* fence, uint64_t value) override;
    void discarded() override;
};

struct Service::Impl {
    // --- settings and capture state (m) --------------------------------------
    mutable std::mutex m;
    Settings settings;
    std::shared_ptr<Segment> current, pendingCapture;
    std::vector<std::shared_ptr<Segment>> finishing;
    struct Probe { int64_t startUs = 0; uint32_t count = 0, width = 0, height = 0, hdrMode = 0; } probe;
    int64_t blockedUntilUs = 0, nextDueUs = 0;
    bool unsupported = false;
    std::wstring error, encoderName;
    uint64_t framesBase = 0, droppedBase = 0;
    CaptureFanout fanout;

    // --- the stream in the ring (streamMutex) --------------------------------
    std::mutex streamMutex;
    PacketRing ring;
    VideoInfo video;
    uint64_t generation = 0, nextGeneration = 1;
    uint32_t streamBitrateMbps = 0;

    // --- audio ----------------------------------------------------------------
    std::atomic<bool> audioActive{false}, audioFailed{false};
    struct AudioBlock { std::vector<float> pcm; size_t frames = 0; sink::AudioFormat format; int64_t playoutUs = 0; };
    std::mutex audioMutex;
    std::condition_variable audioCv;
    std::deque<AudioBlock> audioBlocks;
    size_t audioQueued = 0;
    const void* audioRenderer = nullptr;
    int64_t audioRendererUs = 0;
    bool audioStop = false;
    std::thread audioThread;
    std::atomic<uint64_t> audioOverflow{0}, audioRuns{0};
    AudioInfo audioInfo;                // written once by the audio thread, under streamMutex
    // Audio thread only.
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    sink::AudioFormat swrFormat;
    AVAudioFifo* fifo = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* audioPacket = nullptr;
    std::vector<float> planes[2], silence;
    int64_t anchorUs = 0, produced = 0, encodedSamples = 0, compensationEnd = 0;
    double driftEma = 0;
    bool aligned = false;

    // --- saving (saveMutex) ----------------------------------------------------
    std::mutex saveMutex;
    std::thread saver;
    std::atomic<bool> saving{false};
    uint64_t saved = 0, saveFailures = 0;
    std::wstring lastPath, lastErrorDetail;
    double lastSeconds = 0;
    SaveError lastError = SaveError::None;

    Impl() { fanout.owner = this; }

    // ---- capture (engine thread) ---------------------------------------------
    void retire(std::shared_ptr<Segment>& segment) {
        if (!segment) return;
        if (const auto name = segment->encoderText(); !name.empty()) encoderName = name;
        segment->finish();
        framesBase += segment->frames.load(); droppedBase += segment->dropped.load();
        finishing.push_back(std::move(segment));
        segment.reset();
        audioActive = false;
    }
    void reap() {
        for (auto it = finishing.begin(); it != finishing.end();) {
            if (!(*it)->done()) { ++it; continue; }
            it = finishing.erase(it);   // joins the finished encoder thread
        }
    }
    bool record(ID3D12GraphicsCommandList* list, const PresentCapture::Frame& frame) {
        std::lock_guard lock(m);
        if (pendingCapture) { pendingCapture->discardPending(); pendingCapture.reset(); }
        if (!finishing.empty()) reap();
        if (!settings.enabled || !list || !frame.texture) return false;
        const int64_t now = sink::recordingClockUs();
        if (unsupported || now < blockedUntilUs) return false;
        if (current && current->failed.load()) {
            error = current->errorText();
            retire(current);
            blockedUntilUs = now + kRetryUs;
            return false;
        }
        if (current && !current->matches(frame)) {
            veyra::log::info("replay", std::format("picture changed {}x{} hdr={} -> {}x{} hdr={}; the buffer restarts",
                current->width, current->height, current->hdrMode, frame.width, frame.height, frame.hdrMode));
            retire(current);
        }
        if (!current) {
            // Measure the presented rate first: it sets the rate control and the GOP.
            if (!probe.count || probe.width != frame.width || probe.height != frame.height || probe.hdrMode != frame.hdrMode)
                probe = {now, 0, frame.width, frame.height, frame.hdrMode};
            ++probe.count;
            if (now - probe.startUs < kProbeUs) return false;
            const double measured = double(probe.count - 1) * 1e6 / double(now - probe.startUs);
            probe = {};
            if (!open(frame, measured, now)) return false;
        }
        if (settings.fpsLimit) {
            const int64_t interval = 1000000 / int64_t(settings.fpsLimit);
            if (nextDueUs && now + interval / 4 < nextDueUs) return false;
            nextDueUs = (nextDueUs && now - nextDueUs < interval) ? nextDueUs + interval : now + interval;
        }
        if (!current->capture(list, frame, now)) return false;
        pendingCapture = current;
        return true;
    }
    bool open(const PresentCapture::Frame& frame, double measuredFps, int64_t now) {
        auto refuse = [&](const std::wstring& reason, bool permanent) {
            error = reason;
            if (permanent) unsupported = true; else blockedUntilUs = now + kRetryUs;
            veyra::log::warn("replay", std::format("cannot buffer: {}", utf8(reason)));
            return false;
        };
        auto segment = std::make_shared<Segment>();
        if (FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&segment->device)))) return refuse(L"cannot get the graphics device", false);
        if (adapterVendor(segment->device.Get()) != 0x10DE) return refuse(L"instant replay needs an NVIDIA GPU (NVENC)", true);
        if (frame.width < 16 || frame.height < 16 || (frame.width & 1) || (frame.height & 1))
            return refuse(std::format(L"picture size {}x{} cannot be encoded", frame.width, frame.height), false);
        segment->owner = this;
        segment->width = frame.width; segment->height = frame.height; segment->hdrMode = frame.hdrMode;
        // H.264 hardware encoders stop at 4096 pixels per side.
        segment->hevc = settings.hevc || frame.hdrMode != 0 || frame.width > 4096 || frame.height > 4096;
        uint32_t fps = uint32_t(std::clamp<long>(std::lround(measuredFps > 1 ? measuredFps : 60.0), 10, 480));
        if (settings.fpsLimit) fps = std::min(fps, settings.fpsLimit);
        segment->fps = fps;
        segment->bitrateMbps = std::min(autoBitrateMbps(frame.width, frame.height, fps, segment->hevc), budgetBitrateMbps(settings));
        { std::lock_guard stream(streamMutex); segment->generation = nextGeneration++; }
        std::wstring reason;
        if (!segment->create(reason)) return refuse(reason, false);
        current = segment;
        error.clear(); nextDueUs = 0;
        veyra::log::info("replay", std::format("encoder session extent={}x{} hdr={} codec={} fps={} (measured {:.1f}) bitrateMbps={} seconds={} capMiB={}",
            segment->width, segment->height, segment->hdrMode, segment->hevc ? "HEVC" : "H264", fps, measuredFps, segment->bitrateMbps,
            settings.seconds, settings.maxMegabytes));
        return true;
    }
    void submitted(ID3D12Fence* fence, uint64_t value) {
        std::lock_guard lock(m);
        if (pendingCapture) { pendingCapture->submittedPending(fence, value); pendingCapture.reset(); }
    }
    void discarded() {
        std::lock_guard lock(m);
        if (pendingCapture) { pendingCapture->discardPending(); pendingCapture.reset(); }
    }

    // ---- encoder thread --------------------------------------------------------
    void streamStarted(const Segment& segment, std::vector<uint8_t> headers) {
        std::lock_guard lock(streamMutex);
        ring.clear();
        generation = segment.generation;
        video.width = segment.width; video.height = segment.height; video.fps = segment.fps;
        video.hevc = segment.hevc; video.hdr = segment.hdrMode != 0; video.headers = std::move(headers);
        streamBitrateMbps = segment.bitrateMbps;
        audioActive = settings.audio && !audioFailed.load();   // settings.audio: benign race, re-read on configure()
    }
    void pushVideo(uint64_t from, std::vector<uint8_t> data, int64_t ptsUs, bool key) {
        Packet packet;
        packet.stream = Stream::Video; packet.ptsUs = ptsUs; packet.key = key;
        packet.data = std::make_shared<const std::vector<uint8_t>>(std::move(data));
        std::lock_guard lock(streamMutex);
        if (from == generation) ring.push(std::move(packet));   // a retiring session must not feed the new stream
    }

    // ---- audio ------------------------------------------------------------------
    void audioIn(const float* pcm, size_t count, const sink::AudioFormat& format, int64_t playoutUs, const void* renderer) {
        if (!audioActive.load(std::memory_order_acquire) || !format.valid() || !count) return;
        const int64_t now = sink::recordingClockUs();
        {
            std::lock_guard lock(audioMutex);
            // One endpoint at a time, as the live recorder does.
            if (audioRenderer && audioRenderer != renderer && now - audioRendererUs < 500000) return;
            audioRenderer = renderer; audioRendererUs = now;
            AudioBlock block;
            block.pcm.assign(pcm, pcm + count * format.channels); block.frames = count; block.format = format; block.playoutUs = playoutUs;
            audioQueued += count; audioBlocks.push_back(std::move(block));
            while (audioQueued > kAudioQueueFrames && !audioBlocks.empty()) { audioQueued -= audioBlocks.front().frames; audioBlocks.pop_front(); ++audioOverflow; }
        }
        audioCv.notify_one();
    }
    void startAudio() {
        if (audioThread.joinable()) return;
        { std::lock_guard lock(audioMutex); audioStop = false; }
        audioThread = std::thread([this] { audioMain(); });
    }
    void stopAudio() {
        if (!audioThread.joinable()) return;
        { std::lock_guard lock(audioMutex); audioStop = true; }
        audioCv.notify_all();
        audioThread.join();
        std::lock_guard lock(audioMutex);
        audioBlocks.clear(); audioQueued = 0; audioRenderer = nullptr;
    }
    static AVCodecContext* openAac() {
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) return nullptr;
        AVCodecContext* ctx = avcodec_alloc_context3(encoder);
        if (!ctx) return nullptr;
        ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; ctx->sample_rate = kAudioRate;
        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&ctx->ch_layout, &stereo);
        ctx->bit_rate = 192000; ctx->time_base = AVRational{1, kAudioRate};
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   // MP4 wants the AudioSpecificConfig out of band
        if (avcodec_open2(ctx, encoder, nullptr) < 0) { avcodec_free_context(&ctx); return nullptr; }
        return ctx;
    }
    int frameSize() const { return codec && codec->frame_size > 0 ? codec->frame_size : 1024; }
    // Once per process: the track parameters and one encoded silent frame.
    bool prepareAudioInfo() {
        AVCodecContext* ctx = openAac();
        if (!ctx) return false;
        AudioInfo info;
        info.sampleRate = kAudioRate; info.frameSamples = ctx->frame_size > 0 ? ctx->frame_size : 1024;
        if (ctx->extradata_size > 0) info.extradata.assign(ctx->extradata, ctx->extradata + ctx->extradata_size);
        AVFrame* frame = av_frame_alloc();
        AVPacket* packet = av_packet_alloc();
        bool ok = frame && packet;
        if (ok) {
            frame->nb_samples = info.frameSamples; frame->format = AV_SAMPLE_FMT_FLTP; frame->sample_rate = kAudioRate;
            const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&frame->ch_layout, &stereo);
            ok = av_frame_get_buffer(frame, 0) >= 0;
        }
        // The first packets carry the encoder's start-up; keep a settled one.
        for (int i = 0, got = 0; ok && i < 8 && got < 3; ++i) {
            if (av_frame_make_writable(frame) < 0) { ok = false; break; }
            av_samples_set_silence(frame->data, 0, frame->nb_samples, 2, AV_SAMPLE_FMT_FLTP);
            frame->pts = int64_t(i) * info.frameSamples;
            if (avcodec_send_frame(ctx, frame) < 0) { ok = false; break; }
            while (avcodec_receive_packet(ctx, packet) >= 0) {
                if (++got == 3) info.silence = std::make_shared<const std::vector<uint8_t>>(packet->data, packet->data + packet->size);
                av_packet_unref(packet);
            }
        }
        av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&ctx);
        if (!ok || !info.valid()) return false;
        std::lock_guard lock(streamMutex);
        audioInfo = std::move(info);
        return true;
    }
    void closeRun() {
        if (codec) avcodec_free_context(&codec);
        if (swr) swr_free(&swr);
        if (fifo) { av_audio_fifo_free(fifo); fifo = nullptr; }
        produced = encodedSamples = compensationEnd = 0; driftEma = 0; aligned = false;
    }
    // A run is one continuous stretch of sound; its sample 0 plays at anchorUs.
    bool openRun(int64_t playoutUs) {
        closeRun();
        codec = openAac();
        fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kAudioRate);
        if (!codec || !fifo) return false;
        if (!audioFrame) {
            audioFrame = av_frame_alloc();
            if (!audioFrame) return false;
            audioFrame->nb_samples = frameSize(); audioFrame->format = AV_SAMPLE_FMT_FLTP; audioFrame->sample_rate = kAudioRate;
            const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&audioFrame->ch_layout, &stereo);
            if (av_frame_get_buffer(audioFrame, 0) < 0) return false;
        }
        if (!audioPacket && !(audioPacket = av_packet_alloc())) return false;
        anchorUs = playoutUs;
        ++audioRuns;
        return true;
    }
    bool configureSwr(const sink::AudioFormat& format) {
        if (swr && format == swrFormat) return true;
        if (swr) {
            if (!convertPcm(nullptr, 0)) return false;   // keep what the old layout buffered
            swr_free(&swr);
        }
        AVChannelLayout in{};
        if (av_channel_layout_from_mask(&in, format.mask) < 0 || in.nb_channels != int(format.channels)) {
            av_channel_layout_uninit(&in); av_channel_layout_default(&in, int(format.channels));
        }
        const AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
        const int rc = swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLTP, kAudioRate, &in, AV_SAMPLE_FMT_FLT, kAudioRate, 0, nullptr);
        av_channel_layout_uninit(&in);
        if (rc < 0 || !swr) return false;
        // Resampling at an equal rate lets swr_set_compensation absorb the
        // endpoint clock's drift against the recording clock.
        av_opt_set_int(swr, "flags", SWR_FLAG_RESAMPLE, 0);
        if (swr_init(swr) < 0) { swr_free(&swr); return false; }
        swrFormat = format; compensationEnd = produced;
        silence.assign(size_t(kAudioRate / 10) * format.channels, 0.0f);
        return true;
    }
    bool convertPcm(const float* pcm, int count) {
        const int capacity = swr_get_out_samples(swr, count) + 32;
        if (capacity <= 0) return true;
        for (auto& plane : planes) if (plane.size() < size_t(capacity)) plane.resize(size_t(capacity));
        uint8_t* out[2] = {reinterpret_cast<uint8_t*>(planes[0].data()), reinterpret_cast<uint8_t*>(planes[1].data())};
        const uint8_t* in[1] = {reinterpret_cast<const uint8_t*>(pcm)};
        const int got = swr_convert(swr, out, capacity, pcm ? in : nullptr, pcm ? count : 0);
        if (got < 0) return false;
        if (got > 0) {
            void* data[2] = {planes[0].data(), planes[1].data()};
            if (av_audio_fifo_write(fifo, data, got) < got) return false;
            produced += got;
        }
        return true;
    }
    bool feedSilence(int64_t count) {
        const int64_t chunk = int64_t(silence.size() / swrFormat.channels);
        while (count > 0) {
            const int n = int(std::min(count, chunk));
            if (!convertPcm(silence.data(), n)) return false;
            count -= n;
        }
        return true;
    }
    int64_t outputPosition() const { return produced + (swr ? swr_get_delay(swr, kAudioRate) : 0); }
    bool encodeFifo() {
        const int size = frameSize();
        while (av_audio_fifo_size(fifo) >= size) {
            if (av_frame_make_writable(audioFrame) < 0) return false;
            audioFrame->nb_samples = size;
            if (av_audio_fifo_read(fifo, reinterpret_cast<void**>(audioFrame->data), size) < size) return false;
            audioFrame->pts = encodedSamples; encodedSamples += size;
            if (avcodec_send_frame(codec, audioFrame) < 0) return false;
            int rc = 0;
            while ((rc = avcodec_receive_packet(codec, audioPacket)) >= 0) {
                Packet packet;
                packet.stream = Stream::Audio; packet.key = true;
                packet.ptsUs = anchorUs + av_rescale(audioPacket->pts, 1000000, kAudioRate);
                packet.durationUs = av_rescale(size, 1000000, kAudioRate);
                packet.data = std::make_shared<const std::vector<uint8_t>>(audioPacket->data, audioPacket->data + audioPacket->size);
                av_packet_unref(audioPacket);
                ring.push(std::move(packet));
            }
            if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return false;
        }
        return true;
    }
    // Places a block on the run's timeline as the live recorder does: exact
    // for the first block, silence or trimming beyond the hard limit, and
    // resampling for the small drift between the two clocks.
    bool processBlock(const AudioBlock& block) {
        if (!codec && !openRun(block.playoutUs)) return false;
        if (!configureSwr(block.format)) return false;
        int64_t start = av_rescale(block.playoutUs - anchorUs, kAudioRate, 1000000);
        int64_t drift = start - outputPosition();
        if (aligned && (drift > kAudioRestartGap || drift < -kAudioRestartGap)) {
            // Paused, another source, an endpoint rebuild: nothing is synthesised
            // here; the muxer fills what a clip needs with silent frames.
            if (!openRun(block.playoutUs) || !configureSwr(block.format)) return false;
            drift = 0;
        }
        size_t offset = 0;
        const bool align = !aligned || drift > kAudioHardGap || drift < -kAudioHardGap;
        if (align && drift > 0) { if (!feedSilence(drift)) return false; drift = 0; }
        else if (align && drift < 0) { const auto trim = size_t(-drift); if (trim >= block.frames) return true; offset = trim; drift = 0; }
        aligned = true;
        driftEma = driftEma * 0.9 + double(drift) * 0.1;
        if (std::abs(driftEma) > kAudioSoftGap && produced >= compensationEnd) {
            const int delta = int(std::clamp(driftEma, -double(kCompensationMax), double(kCompensationMax)));
            if (delta && swr_set_compensation(swr, delta, kCompensationWindow) >= 0) compensationEnd = produced + kCompensationWindow;
        }
        if (!convertPcm(block.pcm.data() + offset * block.format.channels, int(block.frames - offset))) return false;
        return encodeFifo();
    }
    void audioMain() {
        bool ok = true;
        { std::lock_guard lock(streamMutex); ok = audioInfo.valid(); }
        if (!ok) ok = prepareAudioInfo();
        if (!ok) {
            audioFailed = true; audioActive = false;
            veyra::log::warn("replay", "AAC encoder unavailable; replays are saved without sound");
        }
        for (;;) {
            std::deque<AudioBlock> blocks;
            {
                std::unique_lock lock(audioMutex);
                audioCv.wait_for(lock, std::chrono::milliseconds(100), [&] { return audioStop || !audioBlocks.empty(); });
                if (audioStop) break;
                blocks.swap(audioBlocks); audioQueued = 0;
            }
            for (const auto& block : blocks) {
                if (!ok) break;
                if (!processBlock(block)) {
                    ok = false; audioFailed = true; audioActive = false;
                    veyra::log::error("replay", "audio encoding failed; replays continue without sound");
                }
            }
        }
        closeRun();
        if (audioFrame) av_frame_free(&audioFrame);
        if (audioPacket) av_packet_free(&audioPacket);
    }

    // ---- saving -----------------------------------------------------------------
    void saveMain(std::wstring path, Clip clip, VideoInfo info, AudioInfo audio) {
        const auto started = std::chrono::steady_clock::now();
        std::wstring reason;
        const bool ok = writeClip(path, clip, info, audio.valid() ? &audio : nullptr, reason);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (ok) veyra::log::info("replay", std::format("saved path={} seconds={:.2f} videoPackets={} audioPackets={} bytes={} writeMs={:.0f}",
            utf8(path), double(clip.durationUs) / 1e6, clip.videoPackets, clip.audioPackets, clip.bytes, ms));
        else veyra::log::error("replay", std::format("save failed path={} reason={}", utf8(path), utf8(reason)));
        std::lock_guard lock(saveMutex);
        if (ok) { ++saved; lastPath = path; lastSeconds = double(clip.durationUs) / 1e6; lastError = SaveError::None; lastErrorDetail.clear(); }
        else { ++saveFailures; lastError = SaveError::Write; lastErrorDetail = reason; }
        saving = false;
    }
};

void Segment::encoderMain() {
    sink::NvencLiveEncoder encoder;
    sink::NvencLiveEncoder::Config config;
    config.width = width; config.height = height; config.hevc = hevc; config.hdr = hdrMode != 0; config.fps = fps; config.bitrateMbps = bitrateMbps;
    std::vector<ID3D12Resource*> inputs;
    for (auto& slot : slots) inputs.push_back(slot.nv12.Get());
    if (!encoder.open(device.Get(), config, inputs)) {
        fail(L"NVENC cannot start: " + encoder.lastError());
    } else {
        { std::lock_guard lock(textMutex); encoderName = encoder.describe(); }
        owner->streamStarted(*this, encoder.headers());
        encoderReady = true;
        int64_t lastKeyUs = INT64_MIN;
        for (;;) {
            std::vector<unsigned> batch; bool closed = false;
            {
                std::unique_lock lock(jobMutex);
                if (jobs.empty() && !jobsClosed && encoder.inFlight() == 0) jobCv.wait(lock, [&] { return !jobs.empty() || jobsClosed; });
                while (!jobs.empty() && batch.size() + encoder.inFlight() < encoder.capacity()) { batch.push_back(jobs.front()); jobs.pop_front(); }
                closed = jobsClosed && jobs.empty();
            }
            for (const auto index : batch) {
                auto& slot = slots[index];
                // Keyframes by time as well as by count, so a clip can start
                // within kKeyIntervalUs of the requested point at any frame rate.
                const bool key = lastKeyUs == INT64_MIN || slot.ptsUs - lastKeyUs >= kKeyIntervalUs;
                if (key) lastKeyUs = slot.ptsUs;
                if (!encoder.submit(index, slot.fence.Get(), slot.value, slot.ptsUs, key)) { fail(L"NVENC encode failed: " + encoder.lastError()); break; }
            }
            if (failed.load()) break;
            // Hand finished frames to the ring; block on the oldest only when
            // nothing new can be submitted meanwhile.
            bool blocked = false;
            while (encoder.inFlight()) {
                bool more = false; { std::lock_guard lock(jobMutex); more = !jobs.empty(); }
                const bool wait = !(more && encoder.inFlight() < encoder.capacity()) && !blocked;
                sink::NvencLiveEncoder::Packet packet; bool ready = false;
                if (!encoder.takeOldest(packet, wait, kGpuWaitMs, ready)) { fail(L"NVENC encode failed: " + encoder.lastError()); break; }
                if (!ready) break;
                blocked |= wait;
                releaseSlot(packet.input);
                owner->pushVideo(generation, std::move(packet.data), packet.ptsUs, packet.key);
            }
            if (failed.load()) break;
            if (closed && !encoder.inFlight()) { std::lock_guard lock(jobMutex); if (jobs.empty()) break; }
        }
        encoder.close();
    }
    encoderReady = false; closing = true;
    encoderFinished = true;
}

bool CaptureFanout::record(ID3D12GraphicsCommandList* list, const Frame& frame) {
    auto* recorder = inner.load(std::memory_order_acquire);
    innerRecorded = recorder && recorder->record(list, frame);
    ownRecorded = owner->record(list, frame);
    return innerRecorded || ownRecorded;
}
void CaptureFanout::submitted(ID3D12Fence* fence, uint64_t value) {
    if (innerRecorded) { innerRecorded = false; if (auto* recorder = inner.load(std::memory_order_acquire)) recorder->submitted(fence, value); }
    if (ownRecorded) { ownRecorded = false; owner->submitted(fence, value); }
}
void CaptureFanout::discarded() {
    if (innerRecorded) { innerRecorded = false; if (auto* recorder = inner.load(std::memory_order_acquire)) recorder->discarded(); }
    if (ownRecorded) { ownRecorded = false; owner->discarded(); }
}

namespace {
// The audio owner thread's fan-out: the live recorder's tap first, then replay.
struct AudioFanout final : sink::RecordingAudioSink {
    Service::Impl* owner = nullptr;
    std::shared_ptr<sink::RecordingAudioSink> inner;
    void renderedPcm(const float* pcm, size_t frames, const sink::AudioFormat& format, int64_t playoutUs, const void* renderer) override {
        if (inner) { inner->renderedPcm(pcm, frames, format, playoutUs, renderer); inner.reset(); }
        owner->audioIn(pcm, frames, format, playoutUs, renderer);
    }
};
}

Service& Service::instance() {
    // Process lifetime on purpose: engine sessions and audio renderers may
    // call the hooks until the very end of the process.
    static Service* service = [] { auto* s = new Service; installBackend(s); return s; }();
    return *service;
}
Service::Service() : p_(std::make_unique<Impl>()) {}
Service::~Service() = default;

void Service::configure(const Settings& requested) {
    Settings next = requested;
    next.seconds = std::clamp(next.seconds, 5, 300);
    next.maxMegabytes = std::clamp<uint32_t>(next.maxMegabytes, 64, 4096);
    if (next.fpsLimit) next.fpsLimit = std::clamp<uint32_t>(next.fpsLimit, 10, 240);
    auto& p = *p_;
    bool start = false, stop = false;
    {
        std::lock_guard lock(p.m);
        const Settings previous = p.settings;
        if (previous == next) return;
        p.settings = next;
        // The stream is fixed for a session: anything that changes it restarts the buffer.
        const bool restart = !next.enabled || previous.hevc != next.hevc || previous.fpsLimit != next.fpsLimit ||
            previous.maxMegabytes != next.maxMegabytes ||
            (p.current && budgetBitrateMbps(next) < p.current->bitrateMbps);
        if (restart && p.current) p.retire(p.current);
        if (restart) { p.probe = {}; p.blockedUntilUs = 0; }
        if (!next.enabled || !next.audio) p.audioActive = false;
        else if (p.current && p.current->encoderReady.load() && !p.audioFailed.load()) p.audioActive = true;
        start = next.enabled && next.audio;
        stop = !start;
        veyra::log::info("replay", std::format("settings enabled={} seconds={} hevc={} audio={} fpsLimit={} capMiB={}",
            next.enabled, next.seconds, next.hevc, next.audio, next.fpsLimit, next.maxMegabytes));
    }
    {
        std::lock_guard lock(p.streamMutex);
        p.ring.setLimits(limitsOf(next));
        if (!next.enabled) { p.ring.clear(); p.generation = 0; }
    }
    if (start) p.startAudio();
    if (stop) p.stopAudio();
}

Settings Service::settings() const { std::lock_guard lock(p_->m); return p_->settings; }

SaveError Service::save(const std::wstring& directory) {
    auto& p = *p_;
    { std::lock_guard lock(p.m); if (!p.settings.enabled) return SaveError::Disabled; }
    std::lock_guard lock(p.saveMutex);
    if (p.saving.load()) return SaveError::Busy;
    if (p.saver.joinable()) p.saver.join();
    auto refuse = [&](SaveError code, std::wstring detail) {
        ++p.saveFailures; p.lastError = code; p.lastErrorDetail = std::move(detail);
        return code;
    };
    Clip clip; VideoInfo info; AudioInfo audio;
    {
        std::lock_guard stream(p.streamMutex);
        clip = p.ring.snapshot();
        info = p.video; audio = p.audioInfo;
    }
    if (clip.empty()) return refuse(SaveError::Empty, {});
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    if (directory.empty() || !std::filesystem::is_directory(directory, code)) return refuse(SaveError::Directory, directory);
    ULARGE_INTEGER available{};
    if (GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr) && available.QuadPart < uint64_t(clip.bytes) + (uint64_t(64) << 20))
        return refuse(SaveError::DiskSpace, directory);
    const std::wstring base = L"CrispPlay-Tekrar-" + fileStamp();
    std::filesystem::path file;
    for (unsigned attempt = 1;; ++attempt) {
        file = std::filesystem::path(directory) / (base + (attempt > 1 ? std::format(L"-{}", attempt) : L"") + L".mp4");
        if (!std::filesystem::exists(file, code)) break;
    }
    p.saving = true;
    p.saver = std::thread([&p, path = file.wstring(), clip = std::move(clip), info = std::move(info), audio = std::move(audio)]() mutable {
        p.saveMain(std::move(path), std::move(clip), std::move(info), std::move(audio));
    });
    return SaveError::None;
}

Status Service::status() {
    auto& p = *p_;
    Status out;
    {
        std::lock_guard lock(p.m);
        if (!p.finishing.empty()) p.reap();
        out.enabled = p.settings.enabled; out.unsupported = p.unsupported; out.error = p.error;
        out.encoder = p.encoderName; out.frames = p.framesBase; out.dropped = p.droppedBase;
        if (p.current) {
            out.capturing = p.current->encoderReady.load();
            out.width = p.current->width; out.height = p.current->height; out.fps = p.current->fps; out.bitrateMbps = p.current->bitrateMbps;
            out.frames += p.current->frames.load(); out.dropped += p.current->dropped.load();
            if (const auto name = p.current->encoderText(); !name.empty()) out.encoder = name;
        }
    }
    {
        std::lock_guard lock(p.streamMutex);
        const auto stats = p.ring.stats();
        out.bufferedSeconds = double(stats.spanUs) / 1e6; out.bufferedBytes = stats.bytes;
        out.audio = stats.audioPackets > 0;
    }
    {
        std::lock_guard lock(p.saveMutex);
        out.saving = p.saving.load(); out.saved = p.saved; out.saveFailures = p.saveFailures;
        out.lastPath = p.lastPath; out.lastSeconds = p.lastSeconds; out.lastError = p.lastError; out.lastErrorDetail = p.lastErrorDetail;
    }
    return out;
}

void Service::shutdown(uint32_t timeoutMs) {
    auto& p = *p_;
    Settings off = settings();
    off.enabled = false;
    configure(off);
    { std::lock_guard lock(p.saveMutex); if (p.saver.joinable()) p.saver.join(); }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        {
            std::lock_guard lock(p.m);
            p.reap();
            if (p.finishing.empty()) return;
            if (std::chrono::steady_clock::now() >= deadline) {
                // A stuck encoder must not hang the exit: leave its thread running.
                for (auto& segment : p.finishing) {
                    veyra::log::error("replay", "encoder session not finished at exit");
                    if (segment->encoderThread.joinable()) segment->encoderThread.detach();
                    static auto* abandoned = new std::vector<std::shared_ptr<Segment>>;
                    abandoned->push_back(segment);
                }
                p.finishing.clear();
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

PresentCapture* Service::wrapCapture(PresentCapture* inner) {
    p_->fanout.inner.store(inner, std::memory_order_release);
    return &p_->fanout;
}

void Service::sessionEnded() {
    auto& p = *p_;
    std::lock_guard lock(p.m);
    if (p.pendingCapture) { p.pendingCapture->discardPending(); p.pendingCapture.reset(); }
    // The encoder belongs to the session's device; the buffered packets stay
    // and can still be saved.
    if (p.current) p.retire(p.current);
    p.probe = {}; p.blockedUntilUs = 0; p.unsupported = false;
}

bool Service::audioWanted() const { return p_->audioActive.load(std::memory_order_acquire); }

std::shared_ptr<sink::RecordingAudioSink> Service::audioTap(std::shared_ptr<sink::RecordingAudioSink> inner) {
    // Per audio thread and not owning: handing a block over must not allocate.
    thread_local AudioFanout fanout;
    fanout.owner = p_.get();
    fanout.inner = std::move(inner);
    return std::shared_ptr<sink::RecordingAudioSink>(std::shared_ptr<void>(), &fanout);
}

} // namespace crispplay::replay
