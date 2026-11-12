#include "SubsService.h"

#include "crispplay/subs/Json.h"
#include "crispplay/subs/OnlineTranslator.h"
#include "crispplay/subs/SubtitlePipeline.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/ocr/HybridOcr.h"
#include "crispplay/subs/ocr/WindowsOcr.h"
#if CRISPPLAY_SUBS_HAVE_MT
#include "crispplay/subs/mt/OpusMtTranslator.h"
#endif

#include "veyra/Log.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

namespace crispplay::subs::app {
namespace {

int64_t steadyUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring environment(const wchar_t* name) {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(name, buffer, 1024);
    return n > 0 && n < 1024 ? std::wstring(buffer, n) : std::wstring();
}

// The last few hundred values of one timing.
class Window {
public:
    void add(double v) { if (values_.size() >= 400) values_.pop_front(); values_.push_back(v); ++count_; }
    Percentiles get() const {
        Percentiles p; p.count = count_;
        if (values_.empty()) return p;
        std::vector<double> sorted(values_.begin(), values_.end());
        std::sort(sorted.begin(), sorted.end());
        p.p50 = sorted[(sorted.size() - 1) / 2];
        p.p95 = sorted[size_t(double(sorted.size() - 1) * 0.95)];
        return p;
    }
    void clear() { values_.clear(); count_ = 0; }
private:
    std::deque<double> values_;
    uint64_t count_ = 0;
};

// Online engine first (when configured), local engine when it fails or is absent.
class EngineSwitch final : public Translator {
public:
    Translator* local = nullptr;
    std::unique_ptr<Translator> online;
    std::atomic<uint64_t> onlineOk{0}, onlineFailed{0};
    std::function<void(double ms, bool online)> timed;

    std::optional<std::string> translate(const std::string& english) override {
        const int64_t start = steadyUs();
        std::optional<std::string> result;
        bool viaOnline = false;
        if (online) {
            result = online->translate(english);
            if (result) { viaOnline = true; onlineOk.fetch_add(1); } else onlineFailed.fetch_add(1);
        }
        if (!result && local) result = local->translate(english);
        if (result && timed) timed(double(steadyUs() - start) / 1000.0, viaOnline);
        return result;
    }
    void cancel() override {
        if (online) online->cancel();
        if (local) local->cancel();
    }
};

void writeBmp(const std::filesystem::path& path, const ocr::BandImage& image) {
    if (image.empty()) return;
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    const uint32_t bytes = uint32_t(image.width) * 4u * uint32_t(image.height);
    file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + bytes;
    info.biSize = sizeof(info); info.biWidth = image.width; info.biHeight = -image.height; info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    for (int y = 0; y < image.height; ++y) out.write(reinterpret_cast<const char*>(image.row(y)), std::streamsize(image.width) * 4);
}

} // namespace

struct Service::Impl {
    BandCapture capture;

    // Requested configuration (m).
    std::mutex m;
    std::condition_variable cv;
    Settings settings;
    Paths paths;
    uint64_t generation = 0;        // bumped when the pipeline must be rebuilt
    bool quit = false, started = false, finished = false;
    std::thread thread;

    // Status (statusMutex).
    std::mutex statusMutex;
    Status status;
    Window readyMs, convertMs, ocrMs, translateMs, shownMs;

    // The drawn line (drawMutex).
    std::mutex drawMutex;
    std::optional<ShownLine> drawn;
    int64_t drawnUntilUs = 0;
    bool drawnOnScreen = false;
    uint64_t serial = 0;
    std::atomic<double> overlayScale{1.0};
    std::atomic<int> coverOpacity{92};

    // "When did this line first show up in a sample" (worker + translation thread).
    struct Seen { std::string text; int64_t firstUs = 0; };
    std::mutex seenMutex;
    std::deque<Seen> seen;

    // Diagnostics.
    bool trace = false;
    std::filesystem::path dumpDirectory;
    std::mutex eventMutex;
    std::ofstream events;
    std::atomic<double> position{-1.0};
    uint64_t dumpIndex = 0;

    std::atomic<bool> probing{false};

    Impl() {
        trace = !environment(L"CRISPPLAY_SUBS_TRACE").empty();
        dumpDirectory = environment(L"CRISPPLAY_SUBS_DUMP_DIR");
        if (const auto file = environment(L"CRISPPLAY_SUBS_EVENT_LOG"); !file.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
            events.open(std::filesystem::path(file), std::ios::binary | std::ios::trunc);
        }
        if (!dumpDirectory.empty()) { std::error_code ec; std::filesystem::create_directories(dumpDirectory, ec); }
#if CRISPPLAY_SUBS_HAVE_MT
        status.localBuilt = true;
#endif
    }

    void event(const std::string& json) {
        if (!events.is_open()) return;
        std::lock_guard lock(eventMutex);
        events << json << '\n';
        events.flush();
    }
    void setState(State state, std::string detail = {}) {
        {
            std::lock_guard lock(statusMutex);
            if (status.state == state && status.detail == detail) return;
            status.state = state; status.detail = detail;
        }
        veyra::log::info("subs", std::format("state={} {}", int(state), detail));
    }
    void clearDrawn() {
        std::lock_guard lock(drawMutex);
        if (drawn) { drawn.reset(); ++serial; }
    }

    // ---- pipeline output (worker or translation thread) -----------------------
    void output(const SubtitleUpdate& update) {
        const int64_t now = steadyUs();
        if (update.kind == SubtitleUpdate::Kind::Clear) {
            std::lock_guard lock(drawMutex);
            // A translation that arrived after its line left keeps its reading time.
            if (drawn && drawnOnScreen) { drawn.reset(); ++serial; }
            event(std::format("{{\"kind\":\"clear\",\"t\":{},\"pos\":{:.3f}}}", now / 1000, position.load()));
            return;
        }
        if (update.kind == SubtitleUpdate::Kind::Cover) {
            std::lock_guard lock(drawMutex);
            if (drawn) {
                drawn->cover = update.line.cover;
                drawn->frameLeft = update.line.frameLeft; drawn->frameTop = update.line.frameTop;
                drawn->frameRight = update.line.frameRight; drawn->frameBottom = update.line.frameBottom;
                ++serial;
            }
            return;
        }
        {
            std::lock_guard lock(drawMutex);
            drawn = update.line;
            drawnOnScreen = update.durationMs >= 15000;
            drawnUntilUs = now + int64_t(std::max(update.durationMs, 500)) * 1000;
            ++serial;
        }
        // First sample that held this line -> now.
        double latency = -1;
        {
            const std::string source = TextMatch::normalize(update.source);
            const std::string head = source.substr(0, std::max<size_t>(source.size() * 6 / 10, std::min<size_t>(source.size(), 8)));
            std::lock_guard lock(seenMutex);
            for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
                if (head.empty() || it->text.find(head) == std::string::npos) continue;
                latency = double(now - it->firstUs) / 1000.0;
                break;
            }
        }
        {
            std::lock_guard lock(statusMutex);
            ++status.shown;
            if (latency >= 0 && !update.replaces) shownMs.add(latency);
        }
        if (trace)
            veyra::log::info("subs", std::format("shown chars={} speaker={} original={} onScreen={} replaces={} sampleToShownMs={:.0f}",
                update.line.text.size(), update.line.speaker.empty() ? 0 : 1, update.line.original ? 1 : 0,
                update.durationMs >= 15000 ? 1 : 0, update.replaces ? 1 : 0, latency));
        if (events.is_open())
            event(std::format("{{\"kind\":\"show\",\"t\":{},\"pos\":{:.3f},\"source\":{},\"speaker\":{},\"text\":{},\"original\":{},"
                              "\"durationMs\":{},\"replaces\":{},\"latencyMs\":{:.0f},\"cover\":[{:.4f},{:.4f},{:.4f},{:.4f}]}}",
                now / 1000, position.load(), json::quote(update.source), json::quote(update.line.speaker), json::quote(update.line.text),
                update.line.original ? "true" : "false", update.durationMs, update.replaces ? "true" : "false", latency,
                update.line.frameLeft, update.line.frameTop, update.line.frameRight, update.line.frameBottom));
    }

    // ---- worker ---------------------------------------------------------------
    void noteSeen(const OcrFrame& frame, int64_t capturedUs) {
        std::string joined;
        for (const auto& line : frame.lines) { joined += line.text; joined += ' '; }
        const std::string text = TextMatch::normalize(joined);
        std::lock_guard lock(seenMutex);
        if (text.empty()) return;
        // The same text (or one that grew: typewriter) keeps its first time.
        if (!seen.empty()) {
            auto& last = seen.back();
            const std::string head = last.text.substr(0, std::max<size_t>(last.text.size() * 6 / 10, std::min<size_t>(last.text.size(), 8)));
            if (text == last.text || (!head.empty() && text.find(head) != std::string::npos)) { last.text = text; return; }
        }
        seen.push_back({text, capturedUs});
        while (seen.size() > 8) seen.pop_front();
    }

    void run() {
        ocr::WindowsOcrReader::prepareThread();
        std::unique_ptr<ocr::HybridOcrReader> reader;
        std::filesystem::path readerModel;
#if CRISPPLAY_SUBS_HAVE_MT
        std::unique_ptr<mt::OpusMtTranslator> local;
        std::filesystem::path localModel;
#endif
        for (;;) {
            Settings wanted; Paths where; uint64_t current = 0;
            {
                std::unique_lock lock(m);
                cv.wait(lock, [&] { return quit || settings.enabled; });
                if (quit) break;
                wanted = settings; where = paths; current = generation;
            }
            auto changed = [&] { std::lock_guard lock(m); return quit || generation != current; };
            auto waitForChange = [&] { std::unique_lock lock(m); cv.wait(lock, [&] { return quit || generation != current; }); };
            setState(State::Starting);

            // OCR reader (kept across rebuilds while the model path is the same).
            if (!reader || readerModel != where.ocrModel) {
                reader.reset();
                ocr::HybridOptions options;
                std::error_code ec;
                if (!where.ocrModel.empty() && std::filesystem::exists(where.ocrModel, ec)) options.ppocrModel = where.ocrModel;
                reader = std::make_unique<ocr::HybridOcrReader>(options);
                readerModel = where.ocrModel;
            }
            {
                const auto& windows = reader->windowsStatus();
                std::lock_guard lock(statusMutex);
                status.ocrProbed = true; status.ocrAvailable = windows.available; status.ocrEnglish = windows.english;
                status.ocrLanguage = windows.languageTag;
                status.ppocr = reader->ppocrReady(); status.ppocrStatus = reader->ppocrStatus();
            }
            veyra::log::info("subs", std::format("ocr windows={} english={} language={} ppocr={} {}", reader->windowsReady() ? 1 : 0,
                reader->windowsStatus().english ? 1 : 0, reader->windowsStatus().languageTag, reader->ppocrReady() ? 1 : 0, reader->ppocrStatus()));
            if (!reader->windowsReady()) {
                setState(State::NoOcr, reader->windowsStatus().message);
                waitForChange();
                continue;
            }

            // Engines.
            EngineSwitch engine;
            std::string localError;
            double loadMs = 0;
#if CRISPPLAY_SUBS_HAVE_MT
            {
                std::error_code ec;
                const bool present = !where.translatorModel.empty() && std::filesystem::exists(where.translatorModel / "model.bin", ec);
                if (!present) { local.reset(); localError = "model files are missing"; }
                else if (!local || localModel != where.translatorModel) {
                    local = std::make_unique<mt::OpusMtTranslator>(where.translatorModel);
                    localModel = where.translatorModel;
                }
                if (local && !local->load()) { localError = local->lastError(); local.reset(); }
                if (local) { engine.local = local.get(); loadMs = local->loadTimeMs(); }
            }
#else
            localError = "this build has no local translator (CTranslate2 missing at build time)";
#endif
            std::string engineId = LineTranslator::defaultEngineId();
            if (wanted.online) {
                const std::string dataDir = utf8(where.dataDirectory.wstring());
                const OnlineConfig config = loadOnlineConfig(dataDir);
                engine.online = makeOnlineTranslator(dataDir, config);
                if (engine.online)
                    engineId = std::format("online{}-{}-p{}", int(config.provider), config.model, FreeTranslation::PipelineVersion);
            }
            {
                std::lock_guard lock(statusMutex);
                status.localEngine = engine.local != nullptr; status.localError = localError;
                status.onlineEngine = engine.online != nullptr; status.loadMs = loadMs;
                status.onlineOk = status.onlineFailed = 0;
            }
            veyra::log::info("subs", std::format("engines local={} loadMs={:.0f} online={} {}", engine.local ? 1 : 0, loadMs,
                engine.online ? 1 : 0, localError));
            if (!engine.local && !engine.online) {
                setState(State::NoEngine, localError);
                waitForChange();
                continue;
            }
            engine.timed = [this](double ms, bool) { std::lock_guard lock(statusMutex); translateMs.add(ms); };

            PipelineOptions options;
            options.translator.cacheDirectory = where.dataDirectory / "subs-cache";
            options.translator.glossaryDirectory = where.dataDirectory / "subs-glossary";
            options.translator.engineId = engineId;
            {
                SubtitlePipeline pipeline(engine, [this](const SubtitleUpdate& update) { output(update); }, options);
                capture.setRegion(wanted.region);
                capture.setEnabled(true);
                setState(State::Running);

                uint64_t session = capture.session();
                uint64_t lastFingerprint = 0;
                OcrFrame lastFrame;
                int64_t lastSummaryUs = steadyUs(), lastSampleUs = 0;
                BandSample sample;
                while (!changed()) {
                    const bool got = capture.next(sample, 300);
                    if (capture.session() != session) {
                        // The engine session ended: nothing of it may stay on screen.
                        session = capture.session();
                        pipeline.reset(); clearDrawn();
                        lastFingerprint = 0;
                        { std::lock_guard lock(seenMutex); seen.clear(); }
                        if (got && sample.session != session) continue;
                    }
                    const int64_t now = steadyUs();
                    if (got) {
                        lastSampleUs = now;
                        process(*reader, pipeline, sample, lastFingerprint, lastFrame);
                    } else if (lastSampleUs && now - lastSampleUs > 3000000) {
                        // No picture for a while (stopped, minimised): do not leave a stale line up.
                        lastSampleUs = 0;
                        pipeline.reset(); clearDrawn();
                        lastFingerprint = 0;
                    }
                    if (now - lastSummaryUs > 10000000) { lastSummaryUs = now; summary(engine); }
                    {
                        std::lock_guard lock(statusMutex);
                        status.onlineOk = engine.onlineOk.load(); status.onlineFailed = engine.onlineFailed.load();
                    }
                }
                capture.setEnabled(false);
                summary(engine);
                pipeline.reset();
            }
            clearDrawn();
            bool keep;
            { std::lock_guard lock(m); keep = settings.enabled && !quit; }
            if (!keep) {
                // Off: give the memory back (model ~260 MB, OCR network).
                reader.reset();
#if CRISPPLAY_SUBS_HAVE_MT
                local.reset();
#endif
                std::lock_guard lock(statusMutex);
                status.localEngine = false; status.onlineEngine = false; status.ppocr = false;
                status.state = State::Off; status.detail.clear();
            }
        }
        std::lock_guard lock(m);
        finished = true;
        cv.notify_all();
    }

    void process(ocr::HybridOcrReader& reader, SubtitlePipeline& pipeline, BandSample& sample, uint64_t& lastFingerprint, OcrFrame& lastFrame) {
        const int64_t nowMs = steadyUs() / 1000;
        const uint64_t print = fingerprint(sample.image);
        OcrFrame frame;
        double ocrTime = 0, windowsMs = 0, ppocrMs = 0;
        const bool reused = print == lastFingerprint && lastFrame.width == sample.image.width && lastFrame.height == sample.image.height;
        if (reused) {
            frame = lastFrame;   // identical picture (paused, static scene): same reading
            frame.timeMs = nowMs;
        } else {
            ocr::HybridDetail detail;
            frame = reader.read(sample.image, nowMs, &detail);
            ocrTime = detail.totalMs; windowsMs = detail.windowsMs; ppocrMs = detail.ppocrMs;
            frame.width = sample.image.width; frame.height = sample.image.height;
            frame.bandLeft = float(sample.box.x) / float(sample.frameWidth);
            frame.bandTop = float(sample.box.y) / float(sample.frameHeight);
            frame.bandRight = float(sample.box.x + sample.box.width) / float(sample.frameWidth);
            frame.bandBottom = float(sample.box.y + sample.box.height) / float(sample.frameHeight);
            lastFrame = frame; lastFingerprint = print;
        }
        noteSeen(frame, sample.capturedUs);
        pipeline.process(frame);
        {
            std::lock_guard lock(statusMutex);
            ++status.samples;
            if (reused) ++status.reusedSamples; else ocrMs.add(ocrTime);
            readyMs.add(sample.readyMs); convertMs.add(sample.convertMs);
            status.frameWidth = sample.frameWidth; status.frameHeight = sample.frameHeight;
            status.bandWidth = sample.box.width; status.bandHeight = sample.box.height;
            status.hdrMode = sample.hdrMode; status.format = int(sample.format); status.scale = sample.scale;
        }
        if (trace)
            veyra::log::info("subs", std::format("sample band={}x{}/{} format={} hdr={} readyMs={:.1f} convertMs={:.1f} ocrMs={:.1f} (windows {:.1f} ppocr {:.1f}) lines={} reused={}",
                sample.box.width, sample.box.height, sample.scale, int(sample.format), sample.hdrMode, sample.readyMs, sample.convertMs,
                ocrTime, windowsMs, ppocrMs, frame.lines.size(), reused ? 1 : 0));
        if (events.is_open()) {
            std::string lines;
            for (const auto& line : frame.lines) { if (!lines.empty()) lines += ','; lines += json::quote(line.text); }
            event(std::format("{{\"kind\":\"sample\",\"t\":{},\"pos\":{:.3f},\"readyMs\":{:.1f},\"convertMs\":{:.1f},\"ocrMs\":{:.1f},\"reused\":{},\"lines\":[{}]}}",
                nowMs, position.load(), sample.readyMs, sample.convertMs, ocrTime, reused ? "true" : "false", lines));
        }
        if (!dumpDirectory.empty() && !reused)
            writeBmp(dumpDirectory / std::format("band-{:06}.bmp", dumpIndex++), sample.image);
    }

    void summary(const EngineSwitch& engine) {
        Percentiles ready, convert, read, translate, shown; Status s;
        {
            std::lock_guard lock(statusMutex);
            ready = readyMs.get(); convert = convertMs.get(); read = ocrMs.get(); translate = translateMs.get(); shown = shownMs.get();
            s = status;
        }
        if (!s.samples) return;
        const CaptureStats c = capture.stats();
        veyra::log::info("subs", std::format(
            "timing samples={} reused={} shown={} readyMs p50={:.1f} p95={:.1f} convertMs p50={:.1f} p95={:.1f} ocrMs p50={:.1f} p95={:.1f} "
            "translateMs p50={:.0f} p95={:.0f} n={} sampleToShownMs p50={:.0f} p95={:.0f} n={} recordUs avg={:.1f} max={:.0f} "
            "noSlot={} stale={} failed={} onlineOk={} onlineFailed={} band={}x{}/{} frame={}x{}",
            s.samples, s.reusedSamples, s.shown, ready.p50, ready.p95, convert.p50, convert.p95, read.p50, read.p95,
            translate.p50, translate.p95, translate.count, shown.p50, shown.p95, shown.count, c.recordUsAvg, c.recordUsMax,
            c.noSlot, c.stale, c.failed, engine.onlineOk.load(), engine.onlineFailed.load(),
            s.bandWidth, s.bandHeight, s.scale, s.frameWidth, s.frameHeight));
    }
};

Service& Service::instance() {
    // Process lifetime on purpose: the present chain may call the tap until the end.
    static Service* service = [] { auto* s = new Service; present::addTap(&s->p_->capture); return s; }();
    return *service;
}
Service::Service() : p_(std::make_unique<Impl>()) {}
Service::~Service() = default;

void Service::configure(const Settings& requested, const Paths& paths) {
    auto& p = *p_;
    Settings next = requested;
    next.region = sanitized(next.region);
    next.overlayScale = std::clamp(next.overlayScale, 0.6, 2.0);
    next.coverOpacity = std::clamp(next.coverOpacity, 40, 100);
    p.overlayScale.store(next.overlayScale); p.coverOpacity.store(next.coverOpacity);
    { std::lock_guard lock(p.drawMutex); ++p.serial; }
    std::lock_guard lock(p.m);
    if (p.quit) return;
    const bool rebuild = next.enabled != p.settings.enabled || next.online != p.settings.online || !(paths == p.paths);
    const bool region = !(next.region == p.settings.region);
    p.settings = next; p.paths = paths;
    if (region) p.capture.setRegion(next.region);
    if (rebuild) { ++p.generation; p.capture.wake(); }
    if (next.enabled && !p.started) { p.started = true; p.thread = std::thread([&p] { p.run(); }); }
    p.cv.notify_all();
}

void Service::reloadEngines() {
    auto& p = *p_;
    std::lock_guard lock(p.m);
    ++p.generation; p.capture.wake();
    p.cv.notify_all();
}

Status Service::status() {
    auto& p = *p_;
    std::lock_guard lock(p.statusMutex);
    Status s = p.status;
    s.capture = p.capture.stats();
    s.readyMs = p.readyMs.get(); s.convertMs = p.convertMs.get(); s.ocrMs = p.ocrMs.get();
    s.translateMs = p.translateMs.get(); s.shownMs = p.shownMs.get();
    return s;
}

void Service::probeOcr() {
    auto& p = *p_;
    if (p.probing.exchange(true)) return;
    std::thread([&p] {
        const auto result = ocr::WindowsOcrReader::probe("en-US");
        {
            std::lock_guard lock(p.statusMutex);
            p.status.ocrProbed = true; p.status.ocrAvailable = result.available; p.status.ocrEnglish = result.english;
            p.status.ocrLanguage = result.languageTag;
        }
        p.probing.store(false);
    }).detach();
}

bool Service::current(DrawnLine& out) {
    auto& p = *p_;
    std::lock_guard lock(p.drawMutex);
    if (!p.drawn) return false;
    if (steadyUs() > p.drawnUntilUs) { p.drawn.reset(); ++p.serial; return false; }
    out.line = *p.drawn; out.serial = p.serial;
    out.scale = p.overlayScale.load(); out.opacity = p.coverOpacity.load();
    return true;
}

void Service::setPlaybackPosition(double seconds) { p_->position.store(seconds); }

void Service::shutdown(uint32_t timeoutMs) {
    auto& p = *p_;
    std::unique_lock lock(p.m);
    p.quit = true; p.settings.enabled = false; ++p.generation;
    p.capture.setEnabled(false);
    p.cv.notify_all();
    if (!p.started) return;
    p.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return p.finished; });
    const bool done = p.finished;
    lock.unlock();
    if (done) p.thread.join(); else p.thread.detach();
}

} // namespace crispplay::subs::app
