#include "BandCapture.h"

#include "veyra/Log.h"

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>

namespace crispplay::subs::app {
namespace {
using Microsoft::WRL::ComPtr;

int64_t steadyUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

enum SlotState : int { Free = 0, Recording = 1, Submitted = 2, Reading = 3 };

// One read-back buffer. The owner is whoever moved `state` out of Free /
// Submitted: the engine thread (Recording) or the worker (Reading).
struct Slot {
    std::atomic<int> state{Free};
    std::atomic<bool> retire{false};      // the session ended while the worker was reading
    ComPtr<ID3D12Resource> buffer;
    uint64_t capacity = 0;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    uint64_t sequence = 0;
    // Sample description.
    uint32_t rowPitch = 0;
    uint32_t frameWidth = 0, frameHeight = 0, hdrMode = 0;
    BandBox box;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int64_t capturedUs = 0;
    uint64_t session = 0;

    void release() { buffer.Reset(); fence.Reset(); capacity = 0; }
};
} // namespace

struct BandCapture::Impl {
    std::array<Slot, 3> slots;
    HANDLE wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // One-shot full-frame snapshot (region picker). Same slot life cycle as the band.
    Slot snap;
    std::atomic<bool> snapWanted{false}, snapFailed{false};
    bool snapPending = false;                 // engine thread
    HANDLE snapEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE snapFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Settings (any thread -> engine thread).
    std::atomic<uint64_t> regionBits[2]{};   // left/top, right/bottom packed as floats
    std::atomic<int64_t> intervalUs{250000};

    // Engine thread only.
    int64_t lastUs = 0;
    Slot* pending = nullptr;
    uint64_t sequence = 0;
    DXGI_FORMAT warnedFormat = DXGI_FORMAT_UNKNOWN;
    bool warnedCreate = false;

    // Counters.
    std::atomic<uint64_t> recorded{0}, delivered{0}, noSlot{0}, stale{0}, failed{0}, unsupported{0};
    std::atomic<uint64_t> recordUsSum{0}, recordUsMax{0};

    ~Impl() {
        for (HANDLE h : {wakeEvent, fenceEvent, snapEvent, snapFenceEvent}) if (h) CloseHandle(h);
    }
    // Engine thread: a read-back buffer of at least `need` bytes in `slot`.
    bool ensureBuffer(Slot& slot, ID3D12Resource* texture, uint64_t need) {
        if (slot.buffer && slot.capacity >= need) return true;
        slot.release();
        ComPtr<ID3D12Device> device;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = need; buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        HRESULT hr = texture->GetDevice(IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr))
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&slot.buffer));
        if (FAILED(hr)) {
            if (!warnedCreate) { warnedCreate = true; veyra::log::warn("subs-capture", std::format("read-back buffer failed hr=0x{:X} bytes={}", unsigned(hr), need)); }
            slot.release();
            return false;
        }
        slot.capacity = need;
        return true;
    }

    static uint64_t pack(float a, float b) { uint32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return uint64_t(x) << 32 | y; }
    static void unpack(uint64_t v, float& a, float& b) { const uint32_t x = uint32_t(v >> 32), y = uint32_t(v); memcpy(&a, &x, 4); memcpy(&b, &y, 4); }
    void setRegion(const Region& r) {
        regionBits[0].store(pack(r.left, r.top), std::memory_order_release);
        regionBits[1].store(pack(r.right, r.bottom), std::memory_order_release);
    }
    Region region() const {
        Region r;
        unpack(regionBits[0].load(std::memory_order_acquire), r.left, r.top);
        unpack(regionBits[1].load(std::memory_order_acquire), r.right, r.bottom);
        return r;
    }
    // Lets go of a slot's device objects if nobody is using it right now.
    static void tryRelease(Slot& slot) {
        int expected = Free;
        if (!slot.state.compare_exchange_strong(expected, Recording)) {
            expected = Submitted;
            if (!slot.state.compare_exchange_strong(expected, Recording)) return;
        }
        slot.release();
        slot.retire.store(false);
        slot.state.store(Free);
    }
};

BandCapture::BandCapture() : p_(std::make_unique<Impl>()) { p_->setRegion(Region{}); }
BandCapture::~BandCapture() = default;

void BandCapture::setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); if (!enabled) wake(); }
void BandCapture::setRegion(const Region& region) { p_->setRegion(sanitized(region)); }
void BandCapture::setIntervalMs(int ms) { p_->intervalUs.store(int64_t(std::clamp(ms, 50, 2000)) * 1000, std::memory_order_release); }
void BandCapture::wake() { SetEvent(p_->wakeEvent); }

CaptureStats BandCapture::stats() const {
    CaptureStats s;
    s.recorded = p_->recorded.load(); s.delivered = p_->delivered.load(); s.noSlot = p_->noSlot.load();
    s.stale = p_->stale.load(); s.failed = p_->failed.load(); s.unsupported = p_->unsupported.load();
    s.recordUsMax = double(p_->recordUsMax.load());
    s.recordUsAvg = s.recorded ? double(p_->recordUsSum.load()) / double(s.recorded) : 0;
    return s;
}

bool BandCapture::record(ID3D12GraphicsCommandList* list, const Frame& frame) {
    // The cost on every other frame: two atomic reads and one clock comparison.
    auto& p = *p_;
    const bool bandOn = enabled_.load(std::memory_order_relaxed);
    const bool snapOn = p.snapWanted.load(std::memory_order_relaxed);
    if (!bandOn && !snapOn) return false;
    if (frame.generated || !list || !frame.texture) return false;
    const int64_t now = steadyUs();
    const int64_t interval = p.intervalUs.load(std::memory_order_relaxed);
    const bool bandDue = bandOn && now - p.lastUs >= interval;
    if (!bandDue && !snapOn) return false;
    // Defensive: a record without its reply.
    if (p.pending) { p.pending->state.store(Free); p.pending = nullptr; }
    if (p.snapPending) { p.snap.state.store(Free); p.snapPending = false; }

    const D3D12_RESOURCE_DESC desc = frame.texture->GetDesc();
    const uint32_t bpp = bytesPerPixel(desc.Format);
    if (!bpp || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1) {
        p.unsupported.fetch_add(1, std::memory_order_relaxed);
        p.lastUs = now;
        if (snapOn) { p.snapWanted.store(false); p.snapFailed.store(true); SetEvent(p.snapEvent); }
        if (p.warnedFormat != desc.Format) {
            p.warnedFormat = desc.Format;
            veyra::log::warn("subs-capture", std::format("output format {} cannot be read; subtitle capture is idle", int(desc.Format)));
        }
        return false;
    }
    const uint32_t width = uint32_t(std::min<uint64_t>(desc.Width, frame.width ? frame.width : desc.Width));
    const uint32_t height = std::min<uint32_t>(desc.Height, frame.height ? frame.height : desc.Height);
    auto pitchOf = [bpp](uint32_t w) {
        return (w * bpp + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    };

    // ---- the band (a few times a second while the feature is on)
    Slot* slot = nullptr;
    BandBox box;
    if (bandDue) {
        box = bandBox(p.region(), width, height);
        if (box.empty()) p.lastUs = now;
        else {
            for (auto& candidate : p.slots) {
                int expected = Free;
                if (candidate.state.compare_exchange_strong(expected, Recording)) { slot = &candidate; break; }
            }
            if (!slot) {
                // The worker is behind: drop this sample, try again in half an interval.
                p.noSlot.fetch_add(1, std::memory_order_relaxed);
                p.lastUs = now - interval / 2;
            } else {
                if (slot->retire.exchange(false)) slot->release();
                const uint64_t need = uint64_t(pitchOf(box.width)) * box.height;
                const bool fresh = !slot->buffer || slot->capacity < need;
                if (!p.ensureBuffer(*slot, frame.texture, need)) {
                    slot->state.store(Free); slot = nullptr;
                    p.failed.fetch_add(1, std::memory_order_relaxed);
                    p.lastUs = now + 2000000;   // do not retry every 250 ms
                } else if (fresh) {
                    veyra::log::info("subs-capture", std::format("read-back slot {}x{} format={} hdr={} bytes={} frame={}x{}",
                        box.width, box.height, int(desc.Format), frame.hdrMode, need, width, height));
                }
            }
        }
    }

    // ---- the one-shot snapshot (region picker)
    Slot* snap = nullptr;
    if (snapOn) {
        int expected = Free;
        bool owned = p.snap.state.compare_exchange_strong(expected, Recording);
        // An earlier snapshot nobody collected is simply overwritten.
        if (!owned) { expected = Submitted; owned = p.snap.state.compare_exchange_strong(expected, Recording); }
        if (owned) {
            if (p.snap.retire.exchange(false)) p.snap.release();
            const uint64_t need = uint64_t(pitchOf(width)) * height;
            if (p.ensureBuffer(p.snap, frame.texture, need)) {
                snap = &p.snap;
                veyra::log::info("subs-capture", std::format("snapshot {}x{} format={} hdr={} bytes={}", width, height, int(desc.Format), frame.hdrMode, need));
            } else {
                p.snap.state.store(Free);
                p.snapWanted.store(false); p.snapFailed.store(true); SetEvent(p.snapEvent);
            }
        }
    }
    if (!slot && !snap) return false;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.texture;
    barrier.Transition.Subresource = 0;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = frame.texture; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; source.SubresourceIndex = 0;
    auto copy = [&](Slot& target, const BandBox& area) {
        D3D12_TEXTURE_COPY_LOCATION dest{};
        dest.pResource = target.buffer.Get(); dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dest.PlacedFootprint.Offset = 0;
        dest.PlacedFootprint.Footprint.Format = desc.Format;
        dest.PlacedFootprint.Footprint.Width = area.width; dest.PlacedFootprint.Footprint.Height = area.height;
        dest.PlacedFootprint.Footprint.Depth = 1; dest.PlacedFootprint.Footprint.RowPitch = pitchOf(area.width);
        const D3D12_BOX region{area.x, area.y, 0, area.x + area.width, area.y + area.height, 1};
        list->CopyTextureRegion(&dest, 0, 0, 0, &source, &region);
        target.rowPitch = pitchOf(area.width); target.frameWidth = width; target.frameHeight = height; target.hdrMode = frame.hdrMode;
        target.box = area; target.format = desc.Format; target.capturedUs = now;
        target.session = session_.load(std::memory_order_relaxed);
    };
    if (slot) copy(*slot, box);
    if (snap) copy(*snap, BandBox{0, 0, width, height});
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);

    if (slot) {
        slot->sequence = ++p.sequence;
        p.pending = slot; p.lastUs = now;
    }
    if (snap) p.snapPending = true;
    if (slot) {
        const uint64_t spent = uint64_t(steadyUs() - now);
        p.recorded.fetch_add(1, std::memory_order_relaxed);
        p.recordUsSum.fetch_add(spent, std::memory_order_relaxed);
        if (spent > p.recordUsMax.load(std::memory_order_relaxed)) p.recordUsMax.store(spent, std::memory_order_relaxed);
    }
    return true;
}

void BandCapture::submitted(ID3D12Fence* fence, uint64_t value) {
    auto& p = *p_;
    if (p.snapPending) {
        p.snapPending = false;
        p.snap.fence = fence; p.snap.fenceValue = value;
        p.snap.state.store(Submitted);
        p.snapWanted.store(false);
        SetEvent(p.snapEvent);
    }
    if (!p.pending) return;
    Slot* slot = p.pending; p.pending = nullptr;
    slot->fence = fence; slot->fenceValue = value;
    slot->state.store(Submitted);
    SetEvent(p.wakeEvent);
}

void BandCapture::discarded() {
    auto& p = *p_;
    // The snapshot request stays: the next frame tries again.
    if (p.snapPending) { p.snapPending = false; p.snap.state.store(Free); }
    if (!p.pending) return;
    p.pending->state.store(Free); p.pending = nullptr;
}

void BandCapture::sessionEnded() {
    auto& p = *p_;
    if (p.pending) { p.pending->state.store(Free); p.pending = nullptr; }
    session_.fetch_add(1, std::memory_order_acq_rel);
    // The buffers belong to the session's device. A slot the worker is reading
    // right now is released by the worker when it is done (retire); nothing here waits.
    for (auto& slot : p.slots) { slot.retire.store(true); Impl::tryRelease(slot); }
    if (p.snapPending) { p.snapPending = false; p.snap.state.store(Free); }
    p.snap.retire.store(true); Impl::tryRelease(p.snap);
    p.lastUs = 0; p.warnedFormat = DXGI_FORMAT_UNKNOWN; p.warnedCreate = false;
    SetEvent(p.wakeEvent);
}

bool BandCapture::next(BandSample& out, uint32_t timeoutMs) {
    auto& p = *p_;
    Slot* slot = nullptr;
    auto take = [&] {
        // Newest submitted sample wins; older ones are stale by now.
        Slot* best = nullptr;
        for (auto& candidate : p.slots)
            if (candidate.state.load() == Submitted && (!best || candidate.sequence > best->sequence)) best = &candidate;
        if (!best) return false;
        int expected = Submitted;
        if (!best->state.compare_exchange_strong(expected, Reading)) return false;
        slot = best;
        for (auto& other : p.slots) {
            if (&other == best) continue;
            expected = Submitted;
            if (other.sequence < best->sequence && other.state.compare_exchange_strong(expected, Reading)) {
                other.fence.Reset();
                other.state.store(Free);
                if (other.retire.load()) Impl::tryRelease(other);
                p.stale.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return true;
    };
    if (!take()) {
        if (WaitForSingleObject(p.wakeEvent, timeoutMs) != WAIT_OBJECT_0) return false;
        if (!take()) return false;
    }
    // Own references: the slot may be retired while we wait for the GPU.
    ComPtr<ID3D12Resource> buffer = slot->buffer;
    ComPtr<ID3D12Fence> fence = std::move(slot->fence);
    auto finish = [&](bool ok) {
        slot->state.store(Free);
        if (slot->retire.load()) Impl::tryRelease(*slot);
        (ok ? p.delivered : p.failed).fetch_add(1, std::memory_order_relaxed);
        return ok;
    };
    if (!buffer || !fence) return finish(false);
    if (fence->GetCompletedValue() < slot->fenceValue) {
        if (FAILED(fence->SetEventOnCompletion(slot->fenceValue, p.fenceEvent))) return finish(false);
        if (WaitForSingleObject(p.fenceEvent, 1000) != WAIT_OBJECT_0) return finish(false);
    }
    // UINT64_MAX: the device was removed.
    if (fence->GetCompletedValue() == UINT64_MAX || slot->retire.load()) return finish(false);
    const int64_t ready = steadyUs();
    const uint64_t bytes = uint64_t(slot->rowPitch) * slot->box.height;
    const D3D12_RANGE readRange{0, SIZE_T(bytes)};
    void* data = nullptr;
    if (FAILED(buffer->Map(0, &readRange, &data)) || !data) return finish(false);
    out.image = convertBand(data, slot->rowPitch, slot->box.width, slot->box.height, slot->format, slot->hdrMode);
    const D3D12_RANGE none{0, 0};
    buffer->Unmap(0, &none);
    out.scale = downscaleForOcr(out.image);
    out.frameWidth = slot->frameWidth; out.frameHeight = slot->frameHeight; out.box = slot->box;
    out.format = slot->format; out.hdrMode = slot->hdrMode; out.capturedUs = slot->capturedUs; out.session = slot->session;
    out.readyMs = double(ready - slot->capturedUs) / 1000.0;
    out.convertMs = double(steadyUs() - ready) / 1000.0;
    return finish(!out.image.empty());
}

void BandCapture::requestSnapshot() {
    p_->snapFailed.store(false);
    p_->snapWanted.store(true, std::memory_order_release);
}

bool BandCapture::takeSnapshot(FrameSnapshot& out, uint32_t timeoutMs) {
    auto& p = *p_;
    Slot& slot = p.snap;
    const int64_t deadline = steadyUs() + int64_t(timeoutMs) * 1000;
    for (;;) {
        int expected = Submitted;
        if (slot.state.compare_exchange_strong(expected, Reading)) break;
        const int64_t left = deadline - steadyUs();
        if (p.snapFailed.exchange(false) || left <= 0) { p.snapWanted.store(false); return false; }
        WaitForSingleObject(p.snapEvent, DWORD(std::min<int64_t>(left / 1000 + 1, 100)));
    }
    // Reading: this thread owns the slot until it is Free again.
    ComPtr<ID3D12Resource> buffer = slot.buffer;
    ComPtr<ID3D12Fence> fence = std::move(slot.fence);
    auto finish = [&](bool ok) {
        // A full frame is large (8 MB at 1080p, up to 66 MB at 4K in half floats): let it go.
        buffer.Reset(); fence.Reset();
        slot.release(); slot.retire.store(false);
        slot.state.store(Free);
        return ok;
    };
    if (!buffer || !fence) return finish(false);
    if (fence->GetCompletedValue() < slot.fenceValue) {
        if (FAILED(fence->SetEventOnCompletion(slot.fenceValue, p.snapFenceEvent))) return finish(false);
        if (WaitForSingleObject(p.snapFenceEvent, 1000) != WAIT_OBJECT_0) return finish(false);
    }
    if (fence->GetCompletedValue() == UINT64_MAX || slot.retire.load()) return finish(false);
    const uint64_t bytes = uint64_t(slot.rowPitch) * slot.box.height;
    const D3D12_RANGE readRange{0, SIZE_T(bytes)};
    void* data = nullptr;
    if (FAILED(buffer->Map(0, &readRange, &data)) || !data) return finish(false);
    out.image = convertBand(data, slot.rowPitch, slot.box.width, slot.box.height, slot.format, slot.hdrMode);
    const D3D12_RANGE none{0, 0};
    buffer->Unmap(0, &none);
    out.frameWidth = slot.frameWidth; out.frameHeight = slot.frameHeight;
    out.format = slot.format; out.hdrMode = slot.hdrMode;
    return finish(!out.image.empty());
}

} // namespace crispplay::subs::app
