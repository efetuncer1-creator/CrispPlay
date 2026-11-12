// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests/CouchSubs.Core.Tests/
// ReadingSplitTests.cs, SubtitleBlockTests.cs and OcrNoiseTests.cs (Reading part), sample lines included.
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/Reading.h"

#include <cmath>

using namespace crispplay::subs;

namespace {
const TextBox kArea{0, 0.6, 1, 1};

TextLine line(const std::string& text, double left, double top, double right, double height = 0.07) {
    return {text, TextBox{left, top, right, top + height}, std::nullopt};
}
bool near(double a, double b, double eps = 1e-3) { return std::abs(a - b) < eps; }
}  // namespace

TEST(Reading_FarApartMenuAndGaugeAreSeparated) { CHECK(Reading::isSeparateText(900, 30, 1920)); }

TEST(Reading_MissedWordsInsideASubtitleDoNotSplitIt) {
    CHECK(!Reading::isSeparateText(150, 30, 1920));
    CHECK(!Reading::isSeparateText(200, 30, 1920));
}

TEST(Reading_WideGapOnSmallTextIsStillOneLineUnlessFarAcrossTheScreen) { CHECK(!Reading::isSeparateText(120, 20, 1920)); }

TEST(CenteredBox_PartlyReadCenteredSubtitleIsWidenedSymmetrically) {
    auto box = TextBox{0.45, 0.85, 0.75, 0.9}.centeredOnScreen();
    CHECK(near(box.left, 0.25));
    CHECK(near(box.right, 0.75));
    CHECK_EQ(box.top, 0.85);
}

TEST(CenteredBox_CornerTextIsLeftAlone) {
    TextBox box{0.02, 0.1, 0.2, 0.15};
    CHECK(box == box.centeredOnScreen());
}

TEST(SubtitleBlock_MenuAndCornerTextAreNotSubtitles) {
    const char* items[] = {"MAP", "HELP", "PROGRESS", "PLAYER", "STORY", "ONLINE", "SOCIAL CLUB", "SETTINGS"};
    std::vector<TextLine> lines;
    for (int i = 0; i < 8; ++i)
        lines.push_back(line(items[i], 0.05, 0.05 + i * 0.1, 0.05 + std::string(items[i]).size() * 0.015));
    lines.push_back(line("PHOTO MODE", 0.85, 0.9, 0.97));
    CHECK(Reading(lines).subtitleBlock(kArea).lines().empty());
}

TEST(SubtitleBlock_CenteredSubtitleIsKeptWithoutCornerGauge) {
    Reading reading({line("We'll lose these tracks if we don't hurry.", 0.25, 0.7, 0.75), line("9", 0.02, 0.7, 0.04),
                     line("PHOTO MODE", 0.85, 0.9, 0.97)});
    auto block = reading.subtitleBlock(kArea);
    CHECK_EQ(block.text(), std::string("We'll lose these tracks if we don't hurry."));
    auto box = *block.box();
    CHECK(near(box.left, 0.25) && near(box.top, 0.7) && near(box.right, 0.75) && near(box.bottom, 0.77));
}

TEST(SubtitleBlock_TwoLineSubtitleStaysTogether) {
    Reading reading({line("Mac and John... they were both shot.", 0.3, 0.6, 0.7), line("We need to move.", 0.4, 0.68, 0.6)});
    CHECK_EQ(reading.subtitleBlock(kArea).text(), std::string("Mac and John... they were both shot.\nWe need to move."));
}

TEST(SubtitleBlock_LeftAlignedSentenceIsStillRead) {
    Reading reading({line("Mafioso: We're worried about this one.", 0.05, 0.8, 0.5)});
    CHECK_EQ(reading.subtitleBlock(kArea).text(), std::string("Mafioso: We're worried about this one."));
}

TEST(SubtitleBlock_LinesWithoutPlaceAreUntouched) {
    auto reading = Reading::fromText("MAP\nHELP");
    CHECK_EQ(reading.subtitleBlock(kArea).text(), reading.text());
}

TEST(SubtitleBlock_ButtonAtTheEndIsAPrompt) {
    CHECK(ButtonPrompts::isPrompt("PHOTO MODE L1 + R1"));
    CHECK(ButtonPrompts::isPrompt("PHOTO MODE [L1] + [R3]"));
    CHECK(!ButtonPrompts::isPrompt("Hand me the L1."));
}

TEST(ButtonPrompts_CommonShapes) {
    // Shapes named in the CouchSubs ButtonPrompts summary.
    CHECK(ButtonPrompts::isPrompt("R2 Swing"));
    CHECK(ButtonPrompts::isPrompt("L1 + R1 Spartan Rage"));
    CHECK(ButtonPrompts::isPrompt("△ Pick up"));
    CHECK(ButtonPrompts::isPrompt("Press ✕ to jump"));
    CHECK(ButtonPrompts::isPrompt("X Interact"));
    CHECK(!ButtonPrompts::isPrompt("Press L to aim."));
    CHECK(!ButtonPrompts::isPrompt("Hold the gate."));
}

TEST(Reading_DropsLinesThatWereOnlyNoise) {
    Reading reading({TextLine{"We go now.", std::nullopt, std::nullopt}, TextLine{"ou$3e 2aljd", std::nullopt, std::nullopt}});
    CHECK_EQ(reading.withoutOcrNoise().text(), std::string("We go now."));
}

TEST(Reading_FromFrameKeepsPixelBoxes) {
    OcrFrame frame{1000, 1000, 200, {{"Hold the gate.", Rect{250, 100, 500, 40}}, {"Now!", Rect{450, 150, 100, 40}}}};
    auto reading = Reading::fromFrame(frame);
    auto box = *reading.box();
    CHECK(near(box.left, 0.25) && near(box.top, 0.5) && near(box.right, 0.75) && near(box.bottom, 0.95));
    auto pixels = reading.pixelBox();
    CHECK_EQ(pixels.x, 250.f);
    CHECK_EQ(pixels.bottom(), 190.f);
}
