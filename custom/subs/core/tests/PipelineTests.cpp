// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests/CouchSubs.Core.Tests/
// SubtitlePipelineTests.cs (queue, cancel and staleness cases; sample lines included), plus
// CrispPlay's overlay requirements (cover rectangle, original line, speaker).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/SubtitlePipeline.h"
#include "crispplay/subs/Unicode.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>

using namespace crispplay::subs;
using Kind = SubtitleUpdate::Kind;

namespace {

struct TestClock {
    std::atomic<int64_t> now{1'000'000};
    std::function<int64_t()> read() { return [this] { return now.load(); }; }
};

// Line-level fake (CouchSubs FakeTranslator): "TR: " + text, optional failures.
class FakeTranslator : public Translator {
public:
    bool error = false;
    int failuresLeft = 0;
    std::vector<std::string> requested;
    std::mutex mutex;
    std::optional<std::string> translate(const std::string& text) override {
        std::lock_guard lock(mutex);
        requested.push_back(text);
        if (error) return std::nullopt;
        if (failuresLeft > 0) { --failuresLeft; return std::nullopt; }
        return "TR: " + text;
    }
    size_t count() { std::lock_guard lock(mutex); return requested.size(); }
};

// Holds translations until the test releases them (CouchSubs GatedTranslator); honours cancel().
class GatedTranslator : public Translator {
public:
    std::vector<std::string> requested;
    std::optional<std::string> translate(const std::string& text) override {
        std::unique_lock lock(mutex_);
        requested.push_back(text);
        if (open_) return "TR: " + text;
        waiting_ = text;
        cancelled_ = false;
        changed_.notify_all();
        changed_.wait(lock, [&] { return open_ || released_ == text || cancelled_; });
        waiting_.reset();
        released_.reset();
        if (cancelled_) { cancelled_ = false; return std::nullopt; }
        return "TR: " + text;
    }
    void cancel() override {
        std::lock_guard lock(mutex_);
        if (waiting_) cancelled_ = true;
        changed_.notify_all();
    }
    void open() {
        std::lock_guard lock(mutex_);
        open_ = true;
        changed_.notify_all();
    }
    // Finishes this line's translation (waits until it is the running one).
    void release(const std::string& text) {
        std::unique_lock lock(mutex_);
        const bool ok = changed_.wait_for(lock, std::chrono::seconds(3), [&] { return waiting_ == text; });
        if (!ok) { subtest::fail(__FILE__, __LINE__, "never asked to translate: " + text); return; }
        released_ = text;
        changed_.notify_all();
    }
    std::vector<std::string> requests() { std::lock_guard lock(mutex_); return requested; }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_ = false, cancelled_ = false;
    std::optional<std::string> waiting_, released_;
};

// Returns a fixed Turkish line: geometry tests must not depend on the similarity check.
class ConstTranslator : public Translator {
public:
    std::optional<std::string> translate(const std::string&) override { return "Kapıyı hemen kapat, gidiyoruz."; }
};

struct Sink {
    std::mutex mutex;
    std::vector<SubtitleUpdate> updates;
    SubtitlePipeline::Output output() {
        return [this](const SubtitleUpdate& u) { std::lock_guard lock(mutex); updates.push_back(u); };
    }
    std::vector<std::string> sent() {
        std::lock_guard lock(mutex);
        std::vector<std::string> out;
        for (auto& u : updates)
            if (u.kind == Kind::Show) out.push_back(u.line.speaker.empty() ? u.line.text : u.line.speaker + ": " + u.line.text);
        return out;
    }
    std::vector<SubtitleUpdate> shows() {
        std::lock_guard lock(mutex);
        std::vector<SubtitleUpdate> out;
        for (auto& u : updates)
            if (u.kind == Kind::Show) out.push_back(u);
        return out;
    }
    int clears() {
        std::lock_guard lock(mutex);
        return static_cast<int>(std::count_if(updates.begin(), updates.end(), [](auto& u) { return u.kind == Kind::Clear; }));
    }
};

// Text rows without boxes, like CouchSubs' FakeReader.
OcrFrame textFrame(const std::string& text) {
    OcrFrame f;
    for (const auto& row : uni::split(uni::decode(text), U'\n')) f.lines.push_back({uni::encode(row), {}, -1.f});
    return f;
}

PipelineOptions lineLevel(TestClock* clock = nullptr) {
    PipelineOptions o;
    o.translator.textPipeline = false;  // fakes translate whole lines, like CouchSubs' ITextTranslator
    if (clock) o.clock = clock->read();
    return o;
}

// 20 reads a second: with a clock it advances 50 ms after each frame.
void run(SubtitlePipeline& p, const std::vector<std::string>& texts, bool waitEach = true, TestClock* clock = nullptr) {
    for (const auto& t : texts) {
        p.process(textFrame(t));
        if (waitEach) p.waitForTranslations();
        if (clock) clock->now += 50;
    }
}

std::vector<std::string> lines(std::initializer_list<std::pair<const char*, int>> runs) {
    std::vector<std::string> out;
    for (auto& [text, count] : runs)
        for (int i = 0; i < count; ++i) out.push_back(text);
    return out;
}

bool waitUntil(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

using Strings = std::vector<std::string>;

}  // namespace

TEST(Pipeline_ButtonPromptsAreNotTranslated) {
    Sink sink;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"R2 Swing", "R2 Swing", "Hold the gate\nR2 Swing", "Hold the gate\nR2 Swing"});
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate"});
}

TEST(Pipeline_StableTextIsTranslatedAndSentOnce) {
    Sink sink;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"Hold the gate", "Hold the gate", "Hold the gate"});
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate"});
    CHECK(p.lastPair() && p.lastPair()->first == "Hold the gate");
}

TEST(Pipeline_MissingModelSendsNothing) {
    Sink sink;
    FakeTranslator tr;
    tr.error = true;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"Hold the gate", "Hold the gate"});
    CHECK(sink.sent().empty());
    CHECK(p.lastFailed());
}

TEST(Pipeline_FailedTranslationIsRetriedWhileLineStaysOnScreen) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    tr.failuresLeft = 1;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate", 25}}), true, &clock);
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate"});
}

TEST(Pipeline_FailedTranslationRetriesAreRateLimited) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    tr.error = true;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate", 40}}), true, &clock);
    CHECK(tr.count() >= 2 && tr.count() <= 3);
}

TEST(Pipeline_ShortReadingGapDoesNotClear) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate", 2}, {"", 2}, {"Hold the gate", 1}}), true, &clock);
    CHECK_EQ(sink.clears(), 0);
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate"});
}

TEST(Pipeline_FlickerShorterThanClearTimeNeitherClearsNorResends) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate", 3}, {"", 15}, {"Hold the gate", 3}, {"", 10}, {"Hold the gate", 3}}), true, &clock);
    CHECK_EQ(sink.clears(), 0);
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate"});
    CHECK_EQ(tr.count(), size_t(1));
}

TEST(Pipeline_SubtitleGoneForClearTimeClears) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    auto texts = lines({{"Hold the gate", 2}, {"", 17}});
    run(p, Strings(texts.begin(), texts.begin() + 18), true, &clock);
    CHECK_EQ(sink.clears(), 0);
    run(p, {""}, true, &clock);
    CHECK_EQ(sink.clears(), 1);
}

TEST(Pipeline_LineReturningSoonAfterClearIsRestoredWithoutTranslating) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate.", 2}, {"", 20}, {"Hold the gate.", 2}}), true, &clock);
    CHECK_EQ(sink.clears(), 1);
    CHECK_EQ(sink.sent(), (Strings{"TR: Hold the gate.", "TR: Hold the gate."}));
    auto shows = sink.shows();
    CHECK(shows.size() == 2 && shows[0].durationMs == 15000 && shows[1].durationMs == 15000);
    CHECK_EQ(tr.count(), size_t(1));
}

TEST(Pipeline_LineReturningLaterIsTranslatedAgain) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate.", 2}, {"", 50}, {"Hold the gate.", 2}}), true, &clock);
    CHECK_EQ(tr.count(), size_t(2));
}

TEST(Pipeline_TwoSpeakersWithOverlappingWordsAreBothTranslated) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Where are you going, boy?", 3}, {"Where are you going?", 3}}), true, &clock);
    CHECK_EQ(sink.sent(), (Strings{"TR: Where are you going, boy?", "TR: Where are you going?"}));
}

TEST(Pipeline_ReadingContinuesWhileTranslating) {
    Sink sink;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"Hold the gate", "Hold the gate", "Run", "Run"}, false);
    tr.open();
    p.waitForTranslations();
    CHECK_EQ(tr.requests(), (Strings{"Hold the gate", "Run"}));
}

TEST(Pipeline_EarlierLineIsShownWhenItsTranslationArrivesFirst) {
    Sink sink;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"Hold the gate.", "Run!"}, false);
    tr.release("Hold the gate.");
    CHECK(waitUntil([&] { return sink.sent().size() == 1; }));
    tr.release("Run!");
    p.waitForTranslations();
    CHECK_EQ(sink.sent(), (Strings{"TR: Hold the gate.", "TR: Run!"}));
}

TEST(Pipeline_FastLinesAreAllTranslatedInOrder) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, {"Hold the gate.", "Run!", "Get down!"}, false, &clock);
    tr.release("Hold the gate.");
    tr.release("Run!");
    tr.release("Get down!");
    p.waitForTranslations();
    CHECK_EQ(tr.requests(), (Strings{"Hold the gate.", "Run!", "Get down!"}));
    CHECK_EQ(sink.sent(), (Strings{"TR: Hold the gate.", "TR: Run!", "TR: Get down!"}));
}

TEST(Pipeline_OldestWaitingLineIsSkippedWhenTheQueueIsFull) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, {"Hold the gate.", "Run!", "Get down!", "Now!"}, false, &clock);
    tr.open();
    p.waitForTranslations();
    CHECK_EQ(tr.requests(), (Strings{"Hold the gate.", "Get down!", "Now!"}));
}

TEST(Pipeline_SlowTranslationsOfQuickLinesAreStillShown) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, {"Hold the gate.", "Run!", "Get down!"}, false, &clock);
    clock.now += 2000;
    tr.release("Hold the gate.");
    tr.release("Run!");
    tr.release("Get down!");
    p.waitForTranslations();
    CHECK_EQ(sink.sent(), (Strings{"TR: Hold the gate.", "TR: Run!", "TR: Get down!"}));
    std::vector<int> durations;
    for (auto& s : sink.shows()) durations.push_back(s.durationMs);
    CHECK_EQ(durations, (std::vector<int>{SubtitlePipeline::readingTime("TR: Hold the gate."), SubtitlePipeline::readingTime("TR: Run!"), 15000}));
}

TEST(Pipeline_GrowingLineSupersedesItsPartialTranslation) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Run to", 2}}), false, &clock);
    // Port note: CouchSubs starts the first translation synchronously; here the worker thread
    // picks it up, and a request superseded before it started is skipped (never translated).
    // Wait until it runs so that the cancel path is tested.
    CHECK(waitUntil([&] { return tr.requests().size() == 1; }));
    run(p, lines({{"Run to the boat!", 2}}), false, &clock);
    tr.release("Run to the boat!");
    p.waitForTranslations();
    CHECK_EQ(tr.requests(), (Strings{"Run to", "Run to the boat!"}));
    CHECK_EQ(sink.sent(), Strings{"TR: Run to the boat!"});
}

TEST(Pipeline_RevisedLineReplacesTheShownTranslation) {
    Sink sink;
    TestClock clock;
    FakeTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Run to", 2}, {"Run to the boat!", 2}, {"Get down now!", 2}}), true, &clock);
    CHECK_EQ(sink.sent(), (Strings{"TR: Run to", "TR: Run to the boat!", "TR: Get down now!"}));
    std::vector<bool> replaces;
    for (auto& s : sink.shows()) replaces.push_back(s.replaces);
    CHECK_EQ(replaces, (std::vector<bool>{false, true, false}));
}

TEST(Pipeline_LateTranslationOfReplacedLineIsDropped) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, {"Hold the gate.", "Run!"}, false, &clock);
    clock.now += SubtitlePipeline::LateResultMs + 100;
    tr.release("Hold the gate.");
    tr.release("Run!");
    p.waitForTranslations();
    CHECK_EQ(sink.sent(), Strings{"TR: Run!"});
}

TEST(Pipeline_TranslationArrivingAfterLineLeftIsShownBriefly) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate.", 1}, {"", 17}}), false, &clock);
    CHECK_EQ(sink.clears(), 1);
    tr.open();
    p.waitForTranslations();
    CHECK_EQ(sink.sent(), Strings{"TR: Hold the gate."});
    auto shows = sink.shows();
    CHECK(!shows.empty() && shows[0].durationMs == 2000 + static_cast<int>(std::string("TR: Hold the gate.").size()) * 60);
}

TEST(Pipeline_TranslationArrivingLongAfterLineLeftIsDropped) {
    Sink sink;
    TestClock clock;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    run(p, lines({{"Hold the gate.", 1}, {"", 17}}), false, &clock);
    CHECK_EQ(sink.clears(), 1);
    clock.now += SubtitlePipeline::LateResultMs + 100;
    tr.open();
    p.waitForTranslations();
    CHECK(sink.sent().empty());
}

TEST(Pipeline_TranslationArrivingAfterStopIsDropped) {
    Sink sink;
    GatedTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    run(p, {"Hold the gate", "Hold the gate"}, false);
    p.reset();
    tr.open();
    p.waitForTranslations();
    CHECK(sink.sent().empty());
}

// --- CrispPlay overlay requirements -----------------------------------------------------------

namespace {
// A 1000x300 band at the bottom centre of the frame (CouchSubs' default region).
OcrFrame boxedFrame(std::vector<OcrLine> lines) {
    OcrFrame f;
    f.width = 1000;
    f.height = 300;
    f.bandLeft = 0.14f;
    f.bandTop = 0.62f;
    f.bandRight = 0.86f;
    f.bandBottom = 0.97f;
    f.lines = std::move(lines);
    return f;
}
}  // namespace

TEST(Cover_CoversTheUnionOfAllSubtitleLineBoxes) {
    Sink sink;
    ConstTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    // Two rows of different width and a short third row; OCR text deliberately differs from
    // any translation, the cover comes from geometry only.
    const Rect a{180, 150, 640, 30}, b{300, 186, 400, 30}, c{420, 222, 160, 30};
    p.process(boxedFrame({{"MINE GUARD: Romano, you done with", a}, {"your count? Delivery's here,", b}, {"Strùnzu!", c}}));
    p.waitForTranslations();
    auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    const Rect cover = shows[0].line.cover;
    CHECK(cover.contains(a) && cover.contains(b) && cover.contains(c));
    CHECK(cover.contains(a.united(b).united(c)));
    // Margin around the union, inside the band.
    CHECK(cover.x < 180 && cover.y < 150 && cover.bottom() > 252 && cover.right() > 820);
    CHECK(cover.x >= 0 && cover.right() <= 1000 && cover.y >= 0 && cover.bottom() <= 300);
    CHECK(shows[0].line.frameLeft > 0.14f && shows[0].line.frameRight < 0.86f && shows[0].line.frameTop > 0.62f);
}

TEST(Cover_ExcludesPromptsAndCornerText) {
    Sink sink;
    ConstTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel());
    const Rect line{300, 200, 400, 30};
    p.process(boxedFrame({{"Get in the car, Enzo.", line}, {"R2 Swing", Rect{450, 260, 100, 25}}, {"9", Rect{5, 10, 15, 25}}}));
    p.waitForTranslations();
    auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    CHECK(shows[0].line.cover.contains(line));
    CHECK(shows[0].line.cover.bottom() < 260);  // the prompt below is not covered
    CHECK(shows[0].line.cover.x > 20);          // the corner gauge is not covered
}

TEST(Cover_GrowsWhenALaterReadingIsWider) {
    Sink sink;
    TestClock clock;
    ConstTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    // First reading misses the last word on a bright background, the next one reads it.
    const Rect narrow{300, 200, 300, 30}, wide{300, 200, 420, 30};
    p.process(boxedFrame({{"Hold the gate, we are leaving.", narrow}}));
    p.waitForTranslations();
    clock.now += 250;
    p.process(boxedFrame({{"Hold the gate, we are leaving now.", wide}}));
    p.waitForTranslations();
    clock.now += 250;
    p.process(boxedFrame({{"Hold the gate, we are leaving.", narrow}}));
    p.waitForTranslations();
    std::lock_guard lock(sink.mutex);
    CHECK(!sink.updates.empty());
    if (sink.updates.empty()) return;
    const auto& last = sink.updates.back();
    CHECK(last.line.cover.contains(wide));
    // The later, narrower reading never shrinks the cover.
    for (auto& u : sink.updates)
        if (&u != &sink.updates.front() && u.kind != Kind::Clear) CHECK(u.line.cover.contains(wide) || u.line.cover.contains(narrow));
}

TEST(Cover_PartlyReadCenteredLineIsWidenedSymmetrically) {
    Sink sink;
    ConstTranslator tr;
    PipelineOptions o = lineLevel();
    SubtitlePipeline p(tr, sink.output(), o);
    // Only the right half of a centred line was read: x 500..800 of a full-width band.
    OcrFrame f;
    f.width = 1000;
    f.height = 300;
    f.lines = {{"we are leaving now.", Rect{500, 200, 300, 30}}};
    p.process(f);
    p.waitForTranslations();
    auto shows = sink.shows();
    CHECK(!shows.empty() && shows[0].line.cover.x <= 200.f + 1 && shows[0].line.cover.right() >= 800);
}

TEST(Cover_FrameCoordinatesFollowTheBand) {
    Sink sink;
    ConstTranslator tr;
    PipelineOptions o = lineLevel();
    o.coverMargin = 0;  // compare with CouchSubs' SubtitleBoxIsSentInFrameCoordinates
    SubtitlePipeline p(tr, sink.output(), o);
    OcrFrame f;
    f.width = 800;
    f.height = 400;
    f.bandLeft = 0.1f;
    f.bandTop = 0.6f;
    f.bandRight = 0.9f;
    f.bandBottom = 1.0f;
    f.lines = {{"Hold the gate.", Rect{200, 200, 400, 200}}};
    p.process(f);
    p.waitForTranslations();
    auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    // 2 px minimum margin: 2/800*0.8 = 0.002 of the frame.
    CHECK(near(shows[0].line.frameLeft, 0.3f) && near(shows[0].line.frameRight, 0.7f));
    CHECK(near(shows[0].line.frameTop, 0.8f) && near(shows[0].line.frameBottom, 1.0f));
}

TEST(Overlay_UnusableTranslationShowsTheOriginalLineWithSpeaker) {
    Sink sink;
    FakeTranslator tr;  // unused by the mapping below
    class NameEngine : public Translator {
    public:
        std::optional<std::string> translate(const std::string& s) override {
            return s == "Strùnzu!" ? std::optional<std::string>("Str ⁇ nzu!") : std::optional<std::string>("Çeviri.");
        }
    } engine;
    SubtitlePipeline p(engine, sink.output());
    const Rect box{350, 200, 300, 30};
    p.process(boxedFrame({{"CESARE: Strùnzu!", box}}));
    p.waitForTranslations();
    auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    CHECK(shows[0].line.original);
    CHECK_EQ(shows[0].line.speaker, std::string("CESARE"));
    CHECK_EQ(shows[0].line.text, std::string("Strùnzu!"));
    CHECK(shows[0].line.cover.contains(box));
    CHECK(shows[0].line.text.find("⁇") == std::string::npos);
}

TEST(Overlay_ClearFollowsTheLineLeaving) {
    Sink sink;
    TestClock clock;
    ConstTranslator tr;
    SubtitlePipeline p(tr, sink.output(), lineLevel(&clock));
    p.process(boxedFrame({{"Hold the gate.", Rect{350, 200, 300, 30}}}));
    p.waitForTranslations();
    for (int i = 0; i < 5; ++i) {
        clock.now += 250;
        p.process(boxedFrame({}));
    }
    CHECK_EQ(sink.clears(), 1);
}
