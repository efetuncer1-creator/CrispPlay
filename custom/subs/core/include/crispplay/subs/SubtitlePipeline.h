// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitlePipeline.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Changes from CouchSubs: input is an already read OcrFrame (OCR runs elsewhere), output is a
// callback for CrispPlay's overlay instead of the TV sender, the cover rectangle is computed from
// the subtitle's line boxes, game packs and dubbing are left out.
#pragma once

#include "crispplay/subs/LineTranslator.h"
#include "crispplay/subs/Reading.h"
#include "crispplay/subs/SubtitleStabilizer.h"
#include "crispplay/subs/Types.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace crispplay::subs {

struct SubtitleUpdate {
    enum class Kind {
        Show,   // draw `line` (replacing what is drawn)
        Clear,  // the subtitle left the screen: remove the line drawn while it was on screen
        Cover,  // same line, its cover grew (a later reading found a wider or extra row)
    };
    Kind kind = Kind::Show;
    ShownLine line;
    // Show: 15000 while the English line is still on screen (a Clear follows), otherwise the
    // reading time of a translation that arrived after the line left.
    int durationMs = 0;
    bool replaces = false;  // revision of the shown line (typewriter growth, better reading)
    std::string source;     // the English line
};

struct PipelineOptions {
    std::function<int64_t()> clock;  // milliseconds; default steady clock
    LineTranslator::Options translator;
    // Cover margin around the union of the line boxes, as a fraction of the median line height
    // (at least 2 px).
    float coverMargin = 0.35f;
    // Widen the cover of a centred subtitle symmetrically around the screen centre (CouchSubs
    // TextBox.CenteredOnScreen): a line read only in part is still covered.
    bool widenCentered = true;
};

// OcrFrame -> subtitle block -> stabilizer -> translation queue -> overlay updates.
// process() never waits for a translation: one translation runs at a time on an internal
// worker thread, at most MaxWaiting lines wait (older ones are skipped), a growing line
// (typewriter) supersedes and cancels the running translation of its start, and a
// translation arriving more than LateResultMs after its line left is dropped.
class SubtitlePipeline {
public:
    using Output = std::function<void(const SubtitleUpdate&)>;
    static constexpr int LateResultMs = 2500;
    static constexpr size_t MaxWaiting = 2;

    // `engine` must outlive the pipeline. `output` is called on the calling thread of
    // process()/reset() or on the worker thread, never under the pipeline's lock.
    SubtitlePipeline(Translator& engine, Output output, PipelineOptions options = {});
    ~SubtitlePipeline();
    SubtitlePipeline(const SubtitlePipeline&) = delete;
    SubtitlePipeline& operator=(const SubtitlePipeline&) = delete;

    void process(const OcrFrame& frame);
    // Blocks until no translation runs or waits (tests, shutdown).
    void waitForTranslations();
    // Stops: running results are dropped, the shown line is cleared.
    void reset();

    // How long a translation that arrived after its line left stays (CouchSubs TV rule).
    static int readingTime(const std::string& text);
    // Cover of a subtitle block in band pixels: union of the line boxes + margin, optionally
    // widened around the screen centre, clamped to the band but never smaller than the union.
    static Rect coverFor(const Reading& block, const OcrFrame& frame, float margin, bool widenCentered);

    std::optional<std::pair<std::string, std::string>> lastPair() const;
    bool lastFailed() const;
    int translationCount() const;  // engine (line) calls made

private:
    struct Request;
    struct Pair { int number; std::string source; std::optional<TranslatedLine> translation; };
    struct Screen { int number; std::string source; Rect cover; };
    using Events = std::vector<SubtitleUpdate>;

    void lineLeft();
    bool enqueue(const std::string& source, const Rect& cover, Events& events);
    bool restore(const std::string& source, const Rect& cover, Events& events);
    void show(const Request& request, const TranslatedLine& line, int duration, Events& events);
    void remember(int number, const std::string& source, std::optional<TranslatedLine> translation);
    bool revisesShown(const std::string& source) const;
    ShownLine makeLine(const TranslatedLine& line, const Rect& cover) const;
    void handleResult(const std::shared_ptr<Request>& request, const std::optional<TranslatedLine>& result,
                      Events& events);
    void worker();
    void emit(const Events& events);

    LineTranslator translator_;
    Output output_;
    PipelineOptions options_;
    std::function<int64_t()> now_;

    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    SubtitleStabilizer stabilizer_;
    int lastRequested_ = 0, lastShown_ = 0;
    std::shared_ptr<Request> inFlight_;
    bool started_ = false;  // inFlight_ has been picked up by the worker
    std::deque<std::shared_ptr<Request>> waiting_;
    std::optional<int> lineOnScreen_;
    std::optional<Screen> screen_;  // geometry of the English line on screen
    std::optional<std::pair<int, ShownLine>> drawn_;  // request number and line last shown
    std::vector<Pair> pairs_;
    int session_ = 0;
    OcrFrame lastFrame_;  // band geometry for cover/frame fractions
    std::optional<std::pair<std::string, std::string>> lastPair_;
    bool lastFailed_ = false;
    int translations_ = 0;
    bool emitting_ = false;  // worker is calling output_ outside the lock
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace crispplay::subs
