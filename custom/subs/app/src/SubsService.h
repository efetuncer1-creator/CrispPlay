// Game subtitle translation: the service that ties the band capture, the OCR
// reader, the translation engines and the text pipeline together, and holds
// the line the overlay draws. Process lifetime; no Qt.
//
//   present chain -> BandCapture (engine thread: copy only)
//                 -> worker thread: wait for the GPU, convert, OCR, SubtitlePipeline::process
//                 -> pipeline's translation thread: OPUS-MT / online engine
//                 -> current(): what the UI thread draws
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "BandCapture.h"
#include "crispplay/subs/Types.h"

namespace crispplay::subs::app {

struct Settings {
    bool enabled = false;
    Region region;
    bool online = false;          // use the online engine first (falls back to the local one)
    double overlayScale = 1.0;    // text size multiplier
    int coverOpacity = 92;        // percent
    bool operator==(const Settings&) const = default;
};

struct Paths {
    std::filesystem::path dataDirectory;     // cache, glossary, online settings
    std::filesystem::path translatorModel;   // OPUS-MT CTranslate2 folder
    std::filesystem::path ocrModel;          // PP-OCR recognition network (.onnx)
    bool operator==(const Paths&) const = default;
};

enum class State {
    Off = 0,
    Starting = 1,     // loading the OCR reader and the model
    Running = 2,
    NoOcr = 3,        // Windows OCR has no usable language
    NoEngine = 4,     // no translation engine: model missing / this build has no translator
};

struct Percentiles { double p50 = 0, p95 = 0; uint64_t count = 0; };

struct Status {
    State state = State::Off;
    std::string detail;                // English, for the log; the UI builds its own text
    bool ocrAvailable = false, ocrEnglish = false, ocrProbed = false;
    std::string ocrLanguage;
    bool ppocr = false;                // second reader active
    std::string ppocrStatus;           // why it is off
    bool localEngine = false;          // OPUS-MT loaded
    bool localBuilt = false;           // this build links the translator
    std::string localError;
    bool onlineEngine = false;         // online engine configured and created
    uint64_t onlineOk = 0, onlineFailed = 0;
    CaptureStats capture;
    uint64_t samples = 0, reusedSamples = 0, shown = 0;
    uint32_t frameWidth = 0, frameHeight = 0, bandWidth = 0, bandHeight = 0, hdrMode = 0;
    int format = 0, scale = 1;
    Percentiles readyMs, convertMs, ocrMs, translateMs, shownMs;   // shownMs: first sample with the line -> drawn
    double loadMs = 0;                 // model load
};

// What the overlay draws now.
struct DrawnLine {
    ShownLine line;
    uint64_t serial = 0;       // changes whenever the line or its cover changes
    double scale = 1.0;
    int opacity = 92;
};

class Service {
public:
    static Service& instance();

    // Any thread. Starts or stops the worker's pipeline as needed.
    void configure(const Settings& settings, const Paths& paths);
    // The online engine settings or key changed: rebuild the engines.
    void reloadEngines();
    Status status();
    // Asks Windows which OCR languages exist (background; result in status()).
    void probeOcr();
    // UI thread: the line to draw, if any.
    bool current(DrawnLine& out);
    // UI thread, for the event log only: playback position in seconds.
    void setPlaybackPosition(double seconds);
    // Region picker: one full-frame copy of the next presented frame (works
    // while the feature is off). takeSnapshot blocks: not on the UI thread.
    void requestSnapshot();
    bool takeSnapshot(FrameSnapshot& out, uint32_t timeoutMs);
    // Region picker: outline `region` on the video while the picker is open (UI thread).
    void setOutline(bool on, const Region& region);
    bool outline(Region& out);
    // Stops the worker, at most timeoutMs.
    void shutdown(uint32_t timeoutMs);

    struct Impl;
private:
    Service();
    ~Service();
    std::unique_ptr<Impl> p_;
};

// Diagnostics, read once at startup:
//   CRISPPLAY_SUBS_TRACE=1            one log line per sample
//   CRISPPLAY_SUBS_DUMP_DIR=<folder>  every read band as PNG (game picture: keep it local)
//   CRISPPLAY_SUBS_EVENT_LOG=<file>   JSON lines of samples and shown lines with their text
//                                     (regression runs; game text: keep it local)

} // namespace crispplay::subs::app
