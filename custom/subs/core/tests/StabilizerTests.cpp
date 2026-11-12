// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests/CouchSubs.Core.Tests/
// SubtitleStabilizerTests.cs and ReadSchedulerTests.cs (sample lines included).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/ReadScheduler.h"
#include "crispplay/subs/SubtitleStabilizer.h"

#include <ostream>

namespace crispplay::subs {
std::ostream& operator<<(std::ostream& out, const StabilizerAction& a) {
    const char* names[] = {"None", "Translate", "Clear", "Restore"};
    return out << names[static_cast<int>(a.kind)] << "(" << a.text << ")";
}
std::ostream& operator<<(std::ostream& out, const ReadPlan& p) {
    return out << "ReadPlan(reuse=" << p.reuse << ", text=" << p.text << ", deep=" << p.deep << ")";
}
}  // namespace crispplay::subs

using namespace crispplay::subs;
using A = StabilizerAction;

namespace {
struct TestClock {
    int64_t now = 1'000'000;
    SubtitleStabilizer::Clock read() { return [this] { return now; }; }
};
const std::string kFull = "Shit, I just came by to see if there's somethin' I can help you with.";
const std::string kWorse = "Shit, I came by to see if there somethinl I can help ya.";
}  // namespace

TEST(Stabilizer_UnfinishedLineIsSentAfterTwoStableFrames) {
    SubtitleStabilizer s;
    CHECK_EQ(s.feed("We do not have time"), A::none());
    CHECK_EQ(s.feed("We do not have time"), A::translate("We do not have time"));
    CHECK_EQ(s.feed("We do not have time"), A::none());
}

TEST(Stabilizer_GarbledRereadOfSentLineDoesNotReemit) {
    SubtitleStabilizer s;
    const std::string first = "Across the deep waters, Pbo went,\nhoping a boat would carry him home.";
    CHECK_EQ(s.feed(first), A::translate(first));
    CHECK_EQ(s.feed("Across the deep waters O went,"), A::none());
    CHECK_EQ(s.feed("Across the deep waters O went,"), A::none());
    CHECK_EQ(s.feed("We should keep moving."), A::translate("We should keep moving."));
}

TEST(Stabilizer_GarbledSentenceIsNotSentOnFirstFrame) {
    SubtitleStabilizer s;
    CHECK_EQ(s.feed("Gtt solkolfege."), A::none());
    CHECK_EQ(s.feed("Go to college."), A::translate("Go to college."));
}

TEST(Stabilizer_GarbledLineIsSentOnlyAfterStayingLongerThanACrossfade) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    const std::string garbled = "- Nat) but, you knpw. shit, I m";
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(s.feed(garbled), A::none());
        clock.now += 100;
    }
    clock.now += SubtitleStabilizer::GarbledStableMs;
    CHECK_EQ(s.feed(garbled), A::translate(garbled));
}

TEST(Stabilizer_CompletedLineAfterSentPartIsTranslated) {
    SubtitleStabilizer s;
    CHECK_EQ(s.feed("We do not have"), A::none());
    CHECK_EQ(s.feed("We do not have"), A::translate("We do not have"));
    CHECK_EQ(s.feed("We do not have time for this."), A::translate("We do not have time for this."));
}

TEST(Stabilizer_WorseRereadOfSentLineIsNotTranslatedAgain) {
    SubtitleStabilizer s;
    CHECK_EQ(s.feed(kFull), A::translate(kFull));
    CHECK_EQ(s.feed(kWorse), A::none());
    CHECK_EQ(s.feed(kFull), A::none());
    CHECK_EQ(s.feed(kWorse), A::none());
}

TEST(Stabilizer_BetterRereadOfSentLineIsTranslated) {
    SubtitleStabilizer s;
    CHECK_EQ(s.feed(kWorse), A::translate(kWorse));
    CHECK_EQ(s.feed(kFull), A::translate(kFull));
    CHECK_EQ(s.feed(kWorse), A::none());
}

TEST(Stabilizer_FinishedSentenceIsSentOnFirstFrame) {
    for (const char* line : {"We do not have time.", "Wait!", "Who are you?", "I know…", "\"Run.\""}) {
        SubtitleStabilizer s;
        CHECK_EQ(s.feed(line), A::translate(line));
        CHECK_EQ(s.feed(line), A::none());
    }
}

TEST(Stabilizer_OcrNoiseDoesNotReemit) {
    SubtitleStabilizer s;
    s.feed("The gate will not hold");
    s.feed("The gate will not hold");
    CHECK_EQ(s.feed("The gale will not hold"), A::none());
    CHECK_EQ(s.feed("The gale will not hold"), A::none());
}

TEST(Stabilizer_FlickeringHudDoesNotSpam) {
    SubtitleStabilizer s;
    int sent = 0;
    for (int i = 0; i < 20; ++i)
        if (s.feed(i % 2 == 0 ? "24 / 120" : "").kind == StabilizerKind::Translate) ++sent;
    CHECK_EQ(sent, 0);
}

TEST(Stabilizer_TypewriterSendsCompletedLine) {
    SubtitleStabilizer s;
    s.feed("We do");
    s.feed("We do not have");
    CHECK_EQ(s.feed("We do not have time for this, boy."), A::translate("We do not have time for this, boy."));
    CHECK_EQ(s.feed("We do not have time for this, boy."), A::none());
}

TEST(Stabilizer_EmptyRegionClearsAfterClearTime) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    s.feed("Hello there");
    s.feed("Hello there");
    CHECK_EQ(s.feed(""), A::none());
    clock.now += SubtitleStabilizer::ClearAfterMs - 1;
    CHECK_EQ(s.feed(""), A::none());
    clock.now += 1;
    CHECK_EQ(s.feed(""), A::clear());
    CHECK_EQ(s.feed(""), A::none());
}

TEST(Stabilizer_ShortFlickerNeitherClearsNorResends) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    s.feed("Hold the gate");
    CHECK_EQ(s.feed("Hold the gate"), A::translate("Hold the gate"));
    for (int i = 0; i < 3; ++i) {
        for (int f = 0; f < 10; ++f) { clock.now += 50; CHECK_EQ(s.feed(""), A::none()); }
        for (int f = 0; f < 4; ++f) { clock.now += 50; CHECK_EQ(s.feed("Hold the gate"), A::none()); }
    }
}

TEST(Stabilizer_LineReturningSoonAfterClearIsRestored) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    CHECK_EQ(s.feed("Hold the gate."), A::translate("Hold the gate."));
    s.feed("");
    clock.now += SubtitleStabilizer::ClearAfterMs;
    CHECK_EQ(s.feed(""), A::clear());
    clock.now += SubtitleStabilizer::RestoreWithinMs;
    CHECK_EQ(s.feed("Hold the gate"), A::none());
    CHECK_EQ(s.feed("Hold the gate"), A::restore("Hold the gate."));
    CHECK_EQ(s.feed("Hold the gate."), A::none());
    s.feed("");
    clock.now += SubtitleStabilizer::ClearAfterMs;
    CHECK_EQ(s.feed(""), A::clear());
}

TEST(Stabilizer_LineReturningLaterIsTranslatedAgain) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    s.feed("Hold the gate.");
    s.feed("");
    clock.now += SubtitleStabilizer::ClearAfterMs;
    CHECK_EQ(s.feed(""), A::clear());
    clock.now += SubtitleStabilizer::RestoreWithinMs + 1;
    CHECK_EQ(s.feed("Hold the gate."), A::translate("Hold the gate."));
}

TEST(Stabilizer_ReplacedLineComingBackIsRestored) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    CHECK_EQ(s.feed("Hold the gate."), A::translate("Hold the gate."));
    clock.now += 100;
    CHECK_EQ(s.feed("Run!"), A::translate("Run!"));
    clock.now += 100;
    CHECK_EQ(s.feed("Hold the gate."), A::restore("Hold the gate."));
    clock.now += 100;
    CHECK_EQ(s.feed("Run!"), A::restore("Run!"));
}

TEST(Stabilizer_TwoSpeakersWithOverlappingWordsAreBothTranslated) {
    const char* cases[][2] = {
        {"Where are you going?", "Where are you going, boy?"},
        {"Where are you going, boy?", "Where are you going?"},
        {"We need to leave this place before the sun goes down.", "We need to leave this place before the sun goes down, boy."},
    };
    for (auto& c : cases) {
        TestClock clock;
        SubtitleStabilizer s(clock.read());
        CHECK_EQ(s.feed(c[0]), A::translate(c[0]));
        clock.now += 50;
        CHECK_EQ(s.feed(c[1]), A::translate(c[1]));
    }
}

TEST(Stabilizer_AlreadyTranslatedIsSkipped) {
    SubtitleStabilizer s;
    CHECK(!s.shouldSend("Kullanıcı Ekle Oturum Açtı", "Kullanıcı Ekle Oturum Açtı"));
    CHECK(s.shouldSend("Add user", "Kullanıcı ekle"));
}

TEST(Stabilizer_ResetClearsOnlyAfterSomethingWasSent) {
    SubtitleStabilizer s;
    CHECK_EQ(s.reset(), A::none());
    s.feed("Hello there");
    s.feed("Hello there");
    CHECK_EQ(s.reset(), A::clear());
    CHECK_EQ(s.reset(), A::none());
}

TEST(Stabilizer_ForgottenLineIsTranslatedAgainAfterBackoff) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    s.feed("Hold the gate");
    CHECK_EQ(s.feed("Hold the gate"), A::translate("Hold the gate"));
    s.forgetSent();
    for (int f = 0; f < 19; ++f) { clock.now += 50; CHECK_EQ(s.feed("Hold the gate"), A::none()); }
    clock.now += 50;
    CHECK_EQ(s.feed("Hold the gate"), A::translate("Hold the gate"));
}

TEST(Stabilizer_BackoffDoesNotDelayANewLine) {
    TestClock clock;
    SubtitleStabilizer s(clock.read());
    s.feed("Hold the gate");
    s.feed("Hold the gate");
    s.forgetSent();
    CHECK_EQ(s.feed("Run!"), A::translate("Run!"));
}

// --- ReadSchedulerTests.cs --------------------------------------------------------------------

namespace {
std::vector<uint8_t> still() { return std::vector<uint8_t>(16, 40); }
}

TEST(ReadScheduler_FirstFrameIsAlwaysRead) { CHECK_EQ(ReadScheduler().plan(still()), ReadPlan::read(true)); }

TEST(ReadScheduler_UnchangedRegionReusesTheLastReading) {
    ReadScheduler s;
    s.plan(still());
    s.record("Hello there");
    CHECK_EQ(s.plan(still()), ReadPlan::reused("Hello there"));
}

TEST(ReadScheduler_VideoNoiseCountsAsUnchanged) {
    ReadScheduler s;
    s.plan(still());
    s.record("");
    auto noisy = still();
    noisy[3] += 8;
    CHECK_EQ(s.plan(noisy), ReadPlan::reused(""));
}

TEST(ReadScheduler_OneChangedSampleTriggersAReading) {
    ReadScheduler s;
    s.plan(still());
    s.record("Hel");
    auto typed = still();
    typed[3] += 40;
    CHECK_EQ(s.plan(typed), ReadPlan::read(false));
}

TEST(ReadScheduler_ReusedReadingIsRefreshedAfterALimit) {
    ReadScheduler s(16, 2, 2);
    s.plan(still());
    s.record("a line");
    CHECK(s.plan(still()).reuse);
    CHECK(s.plan(still()).reuse);
    CHECK_EQ(s.plan(still()), ReadPlan::read(false));
}

TEST(ReadScheduler_SlowerSearchRunsOnlyOnEveryThirdEmptyFrame) {
    ReadScheduler s(16, 20, 3);
    std::vector<bool> deep;
    for (int value = 0; value < 6; ++value) {
        deep.push_back(s.plan(std::vector<uint8_t>(16, static_cast<uint8_t>(value * 40))).deep);
        s.record("");
    }
    CHECK_EQ(deep, (std::vector<bool>{true, false, false, true, false, false}));
}

TEST(ReadScheduler_ResetForgetsThePreviousFrame) {
    ReadScheduler s;
    s.plan(still());
    s.record("old");
    s.reset();
    CHECK_EQ(s.plan(still()), ReadPlan::read(true));
}

TEST(ReadScheduler_AlwaysDisablesReuse) {
    auto s = ReadScheduler::always();
    s.plan(still());
    s.record("");
    CHECK_EQ(s.plan(still()), ReadPlan::read(true));
}
