// Instant replay: the extension point upstream files call (VEYRA_CUSTOM hooks).
//
// Header-only dispatch to a backend installed at startup, so no upstream
// target links anything new. Without a backend (tools, tests) every hook is a
// pass-through and upstream behaviour is unchanged.
#pragma once

#include <atomic>
#include <memory>

#include "veyra/engine/PresentCapture.h"
#include "veyra/sink/RecordingAudioTap.h"

namespace crispplay::replay {

class Backend {
public:
    virtual ~Backend() = default;
    // Engine thread, once per session: the capture the presenter should call.
    // Returns a fan-out that forwards to `inner` (the live recorder) as well.
    virtual veyra::engine::PresentCapture* wrapCapture(veyra::engine::PresentCapture* inner) = 0;
    // Engine thread: the session that owns the device ended.
    virtual void sessionEnded() = 0;
    // Audio owner thread: one atomic read while replay takes no audio.
    virtual bool audioWanted() const = 0;
    // Audio owner thread: the sink to hand this block to; forwards to `inner`
    // (the live recorder's tap, may be null) as well.
    virtual std::shared_ptr<veyra::sink::RecordingAudioSink> audioTap(std::shared_ptr<veyra::sink::RecordingAudioSink> inner) = 0;
};

namespace detail {
inline std::atomic<Backend*> gBackend{nullptr};
}

// The backend must outlive every engine session and audio renderer.
inline void installBackend(Backend* backend) { detail::gBackend.store(backend, std::memory_order_release); }

inline veyra::engine::PresentCapture* wrapCapture(veyra::engine::PresentCapture* inner) {
    auto* backend = detail::gBackend.load(std::memory_order_acquire);
    return backend ? backend->wrapCapture(inner) : inner;
}
inline void sessionEnded() {
    if (auto* backend = detail::gBackend.load(std::memory_order_acquire)) backend->sessionEnded();
}
inline bool audioWanted() {
    auto* backend = detail::gBackend.load(std::memory_order_acquire);
    return backend && backend->audioWanted();
}
inline std::shared_ptr<veyra::sink::RecordingAudioSink> audioTap(std::shared_ptr<veyra::sink::RecordingAudioSink> inner) {
    auto* backend = detail::gBackend.load(std::memory_order_acquire);
    return backend && backend->audioWanted() ? backend->audioTap(std::move(inner)) : inner;
}

} // namespace crispplay::replay
