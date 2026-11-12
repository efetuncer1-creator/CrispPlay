// Present capture chain: the extension point upstream's engine calls
// (VEYRA_CUSTOM hooks in src/engine/EngineController.cpp).
//
// The presenter has one PresentCapture slot, used by the live recorder. This
// header fans that slot out to the recorder plus every tap installed at
// startup (instant replay, subtitle band capture), so features share one
// upstream hook line. Header-only: no upstream target links anything new, and
// without taps (tools, tests) the recorder is returned unchanged.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "veyra/engine/PresentCapture.h"

namespace crispplay::present {

// A consumer of presented frames. record/submitted/discarded follow the
// PresentCapture contract (engine thread, inside the presentation list).
class Tap : public veyra::engine::PresentCapture {
public:
    // Engine thread: the session that owns the device ended. Everything that
    // belongs to that device must be let go of without waiting on the GPU.
    virtual void sessionEnded() = 0;
};

namespace detail {
inline constexpr size_t kMaxTaps = 4;
inline std::array<std::atomic<Tap*>, kMaxTaps> gTaps{};

// Engine thread only (one engine session at a time, like the recorder).
struct Chain final : veyra::engine::PresentCapture {
    veyra::engine::PresentCapture* inner = nullptr;   // the live recorder, may be null
    bool innerRecorded = false;
    std::array<Tap*, kMaxTaps> recorded{};            // taps that recorded into the current list

    bool record(ID3D12GraphicsCommandList* list, const Frame& frame) override {
        innerRecorded = inner && inner->record(list, frame);
        bool any = innerRecorded;
        for (size_t i = 0; i < kMaxTaps; ++i) {
            auto* tap = gTaps[i].load(std::memory_order_acquire);
            recorded[i] = tap && tap->record(list, frame) ? tap : nullptr;
            any = any || recorded[i];
        }
        return any;
    }
    void submitted(ID3D12Fence* fence, uint64_t value) override {
        if (innerRecorded) { innerRecorded = false; inner->submitted(fence, value); }
        for (auto& tap : recorded) if (tap) { tap->submitted(fence, value); tap = nullptr; }
    }
    void discarded() override {
        if (innerRecorded) { innerRecorded = false; inner->discarded(); }
        for (auto& tap : recorded) if (tap) { tap->discarded(); tap = nullptr; }
    }
};
inline Chain gChain;
} // namespace detail

// Startup, before the first engine session. A tap must outlive every session
// (process lifetime). Returns false when the table is full.
inline bool addTap(Tap* tap) {
    for (auto& slot : detail::gTaps) {
        Tap* expected = nullptr;
        if (slot.load(std::memory_order_acquire) == tap) return true;
        if (slot.compare_exchange_strong(expected, tap, std::memory_order_acq_rel)) return true;
    }
    return false;
}

// Engine thread, once per session: the capture the presenter should call.
inline veyra::engine::PresentCapture* wrapCapture(veyra::engine::PresentCapture* inner) {
    bool any = false;
    for (auto& slot : detail::gTaps) any = any || slot.load(std::memory_order_acquire);
    if (!any) return inner;
    detail::gChain.inner = inner;
    return &detail::gChain;
}

// Engine thread: the session ended.
inline void sessionEnded() {
    for (auto& slot : detail::gTaps)
        if (auto* tap = slot.load(std::memory_order_acquire)) tap->sessionEnded();
}

} // namespace crispplay::present
