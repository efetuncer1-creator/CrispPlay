// Subtitle band capture: a tap in the present chain that copies only the
// subtitle region of a presented frame into a small read-back ring, a few
// times a second. The presentation path never waits: no CPU wait on the
// engine thread, no full-frame copy, and a sample is dropped when no slot is
// free. A worker thread (next()) waits for the GPU, maps and converts.
//
// Same pattern as upstream's LiveRecorder / present probe: the copy is
// recorded into the presentation list while the graph output is in COMMON,
// and the fence value reported by submitted() covers it.
//
// Snapshot (region picker): on request, one full-frame copy rides along with
// the next presented real frame, also without any wait on the engine thread;
// takeSnapshot() waits for it on the caller's thread. Works while the feature
// is off; the buffer is let go of once it has been read.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "BandFormat.h"
#include "crispplay/present/Hooks.h"

namespace crispplay::subs::app {

struct BandSample {
    ocr::BandImage image;                  // BGRA8, already downscaled for OCR
    int scale = 1;                         // downscale factor applied (1, 2, 4)
    uint32_t frameWidth = 0, frameHeight = 0;
    BandBox box;                           // where the band sits in the frame (frame pixels)
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t hdrMode = 0;
    int64_t capturedUs = 0;                // steady clock when the copy was recorded
    double readyMs = 0;                    // recorded -> GPU finished the copy
    double convertMs = 0;                  // map + convert + downscale
    uint64_t session = 0;                  // changes with every engine session
};

struct FrameSnapshot {
    ocr::BandImage image;                  // BGRA8, full frame, HDR tone mapped (not downscaled)
    uint32_t frameWidth = 0, frameHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t hdrMode = 0;
};

struct CaptureStats {
    uint64_t recorded = 0;       // copies recorded into a presentation list
    uint64_t delivered = 0;      // samples handed to the worker
    uint64_t noSlot = 0;         // dropped: all slots busy
    uint64_t stale = 0;          // dropped: a newer sample was waiting
    uint64_t failed = 0;         // fence timeout, map failure, lost device
    uint64_t unsupported = 0;    // frames in a format that cannot be read
    double recordUsMax = 0;      // longest record() call (engine thread), microseconds
    double recordUsAvg = 0;      // mean of the sampling record() calls
};

class BandCapture final : public present::Tap {
public:
    BandCapture();
    ~BandCapture() override;
    BandCapture(const BandCapture&) = delete;
    BandCapture& operator=(const BandCapture&) = delete;

    // Any thread.
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }
    void setRegion(const Region& region);
    void setIntervalMs(int ms);
    CaptureStats stats() const;
    // Engine sessions seen ending so far (the worker resets its text state on a change).
    uint64_t session() const { return session_.load(std::memory_order_acquire); }

    // Worker thread: blocks up to timeoutMs for the next sample. False on
    // timeout or when the sample had to be dropped.
    bool next(BandSample& out, uint32_t timeoutMs);
    // Any thread: wakes a blocked next().
    void wake();

    // Any thread: copy the next presented real frame whole (one shot).
    void requestSnapshot();
    // Any thread but the engine's: waits up to timeoutMs for the requested copy
    // and converts it. False when no frame was presented in time (nothing
    // playing, paused source that does not present) or the copy failed; the
    // request is withdrawn then.
    bool takeSnapshot(FrameSnapshot& out, uint32_t timeoutMs);

    // present::Tap (engine thread).
    bool record(ID3D12GraphicsCommandList* list, const Frame& frame) override;
    void submitted(ID3D12Fence* fence, uint64_t value) override;
    void discarded() override;
    void sessionEnded() override;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> session_{1};
};

} // namespace crispplay::subs::app
