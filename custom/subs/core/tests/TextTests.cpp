// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests/CouchSubs.Core.Tests/
// TextMatchTests.cs and OcrNoiseTests.cs (sample lines included).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/Unicode.h"

using namespace crispplay::subs;
namespace TM = crispplay::subs::TextMatch;

// --- Unicode helpers (CrispPlay; .NET behaviour documented in NOTICE.md) ----------------------

TEST(Unicode_RoundTripAndCategories) {
    const std::string s = "Ağaç İstanbul ılık ⁇ 😀";
    CHECK_EQ(uni::encode(uni::decode(s)), s);
    CHECK(uni::isLetter(U'ğ'));
    CHECK(uni::isUpper(U'İ'));
    CHECK(uni::isLower(U'ı'));
    CHECK(!uni::isLetter(U'⁇'));
    CHECK(uni::isPunctuation(U'⁇'));
    CHECK(uni::isWhiteSpace(U' '));
    CHECK(uni::isNumber(U'５'));
    CHECK_EQ(uni::decode("\xff" "a"), std::u32string(U"�a"));
}

TEST(Unicode_InvariantCasingLeavesTurkishDottedAndDotlessI) {
    // .NET (ICU) invariant casing does not map U+0130 to lower or U+0131 to upper.
    CHECK(uni::toLowerInvariant(U'İ') == U'İ');
    CHECK(uni::toUpperInvariant(U'ı') == U'ı');
    CHECK(uni::toLowerInvariant(U'I') == U'i');
    CHECK(uni::toLowerTurkish(U'I') == U'ı');
    CHECK(uni::toLowerTurkish(U'İ') == U'i');
    CHECK(uni::toUpperTurkish(U'i') == U'İ');
    CHECK(uni::toUpperInvariant(U'ç') == U'Ç');
    CHECK(uni::toLowerInvariant(U'Ş') == U'ş');
    // So TextMatch.Normalize keeps a capital İ like CouchSubs on Windows does.
    CHECK_EQ(TM::normalize("MİNER: Not even close."), std::string("mİner not even close"));
}

// --- TextMatchTests.cs ------------------------------------------------------------------------

TEST(TextMatch_NormalizeStripsPunctuationAndCase) {
    CHECK_EQ(TM::normalize("Kratos: We do NOT have time, boy."), std::string("kratos we do not have time boy"));
}

TEST(TextMatch_SimilarToleratesOcrNoise) { CHECK(TM::isSimilar("the gate will not hold", "the gale will not hold")); }
TEST(TextMatch_DifferentLinesAreNotSimilar) { CHECK(!TM::isSimilar("we do not have time", "the gate will not hold")); }
TEST(TextMatch_EmptyIsNeverSimilarToText) { CHECK(!TM::isSimilar("", "abc")); }

TEST(TextMatch_SameSubtitleIgnoresGarbledWords) {
    auto sent = TM::normalize("Across the deep waters, Pbo ou$3e went, hoping a boat would carry him home.");
    auto wordSwap = TM::normalize("Across the deep waters, 2aljd Pbo went, Hoping 3e boat would carry him home now.");
    CHECK(!TM::isSimilar(wordSwap, sent));
    CHECK(TM::isSameSubtitle(wordSwap, sent));
}

TEST(TextMatch_RereadMissingALineIsSameSubtitle) {
    const std::string twoRows = "Across the deep waters, Pbo went,\nhoping a boat would carry him home.";
    auto sent = TM::normalizeRows(twoRows);
    CHECK(TM::isSameSubtitle(TM::normalize(OcrNoise::clean("Across the deep waters2aljd O went,")), sent));
    CHECK(TM::isSameSubtitle(TM::normalize("Across the deep waters, Pbo went,"), sent));
    CHECK(TM::isSameSubtitle(TM::normalize("hoping a boat would carry him home."), sent));
    CHECK(!TM::isSameSubtitle(TM::normalize(twoRows), TM::normalize("Across the deep waters, Pbo went,")));
}

TEST(TextMatch_LineSharingMostWordsIsANewSubtitle) {
    const char* cases[][2] = {
        {"Where are you going?", "Where are you going, boy?"},
        {"Where are you going, boy?", "Where are you going?"},
        {"We need to leave this place before the sun goes down, boy.", "We need to leave this place before the sun goes down."},
        {"Boy, we need to leave this place before the sun goes down.", "We need to leave this place before the sun goes down."},
    };
    for (auto& c : cases) CHECK(!TM::isSameSubtitle(TM::normalize(c[0]), TM::normalizeRows(c[1])));
}

TEST(TextMatch_ShortenedSingleRowIsNotAMissingRow) {
    CHECK(!TM::isSameSubtitle("we have to cross the river", TM::normalizeRows("We have to cross the river before the sun goes down")));
    CHECK(TM::isSameSubtitle("we have to cross the river", TM::normalizeRows("We have to cross the river\nbefore the sun goes down")));
}

TEST(TextMatch_SameSubtitleKeepsDistinctLinesApart) {
    const char* cases[][2] = {
        {"we do not have time", "the gate will not hold"},
        {"we do not have time for this boy and you know it", "we do not have time"},
        {"where is the boy", "where is the ship"},
        {"you should not be here", "i told you to stay with your mother"},
        {"we have to cross the river before the sun goes down", "the river is too deep for us to cross today"},
    };
    for (auto& c : cases) CHECK(!TM::isSameSubtitle(c[0], c[1]));
}

TEST(TextMatch_RevisionsOfTheSameSentence) {
    const char* cases[][2] = {
        {"We do not have time for this.", "We do not have"},
        {"We do not have", "We do not have time for this."},
        {"have time for this, boy.", "We do not have time for this, boy."},
        {"The gate will not hold!", "The gale will not hold"},
        {"Across the deep waters he went, hoping a boat would carry him home.", "Across the deep waters O went, Hoping a boal would carry him home"},
    };
    for (auto& c : cases) CHECK(TM::isRevision(c[0], c[1]));
}

TEST(TextMatch_DifferentSentencesAreNotRevisions) {
    const char* cases[][2] = {
        {"Run to the boat!", "We do not have time for this."},
        {"Where is the ship?", "Where is the boy?"},
        {"No.", "Yes."},
        {"I told you to stay with your mother.", "You should not be here."},
    };
    for (auto& c : cases) CHECK(!TM::isRevision(c[0], c[1]));
}

TEST(TextMatch_GrowthOnlyCountsAddedWords) {
    CHECK(TM::isGrowth("We do not have time.", "We do not"));
    CHECK(!TM::isGrowth("We do not", "We do not have time."));
    CHECK(!TM::isGrowth("We do not have time.", "We do not have time."));
    CHECK(TM::isGrowth("Then we go home.", "Then"));
    CHECK(!TM::isGrowth("Then we go home.", "home"));
    CHECK(TM::isGrowth("Then we go home.", "go home"));
}

// --- OcrNoiseTests.cs -------------------------------------------------------------------------

TEST(OcrNoise_GarbledWordsAreRemoved) {
    CHECK_EQ(OcrNoise::clean("Across the deep waters, Pbo ou$3e went, hoping a boat would carry him home."),
             std::string("Across the deep waters, Pbo went, hoping a boat would carry him home."));
    CHECK_EQ(OcrNoise::clean("Across the deep waters2aljd O went, Hoping a boat."), std::string("Across the deep waters O went, Hoping a boat."));
    CHECK_EQ(OcrNoise::clean("He left 3e, at dawn."), std::string("He left, at dawn."));
}

TEST(OcrNoise_UsualTokensAreKept) {
    for (const char* line : {"Turn on the PS5 now.", "Hold R2-D2 still.", "It costs $500.", "Meet me at 3:00pm.",
                             "The 2nd gate is 100m away.", "Run at 60fps, 4K.", "Score x2!", "Year 1984.", "I can't — wait…",
                             "\"Kratos!\" she said.", "Mk2 rifle, 9.5kg.", "L1 + R1 Spartan Rage"})
        CHECK_EQ(OcrNoise::clean(line), std::string(line));
}

TEST(OcrNoise_StrayPunctuationAndBoxEdgesAreRemoved) {
    const char* cases[][2] = {
        {"?! Where do you think you're going?", "Where do you think you're going?"},
        {"Where do you think you're going ?!", "Where do you think you're going?!"},
        {"Get in the car, Enzo. ?!", "Get in the car, Enzo."},
        {"Wait, ?! Don Salieri wants you.", "Wait, Don Salieri wants you."},
        {". Get in the car.", "Get in the car."},
        {"| Get in the car, Enzo.", "Get in the car, Enzo."},
        {"Get in the car, Enzo. |", "Get in the car, Enzo."},
        {"Leave it. l", "Leave it."},
    };
    for (auto& c : cases) CHECK_EQ(OcrNoise::clean(c[0]), std::string(c[1]));
}

TEST(OcrNoise_MisreadPronounBecomesI) {
    const char* cases[][2] = {
        {"l don't know, Don Salieri.", "I don't know, Don Salieri."},
        {"| don't know.", "I don't know."},
        {"l'm telling you, l saw him.", "I'm telling you, I saw him."},
        {"|'ll be right back.", "I'll be right back."},
        {"You and l've been here before.", "You and I've been here before."},
    };
    for (auto& c : cases) CHECK_EQ(OcrNoise::clean(c[0]), std::string(c[1]));
}

TEST(OcrNoise_RealPunctuationIsKept) {
    for (const char* line : {"What?! The door opened by itself!", "- Where? - Home.", "Wait... what?", "I... I don't know.",
                             "\"Run!\" he said.", "So, you're Enzo?", "... and then he left.", "Wait ... what?", "I said no!",
                             "Leave it, I said.", "Press L to aim."})
        CHECK_EQ(OcrNoise::clean(line), std::string(line));
}

TEST(OcrNoise_CleanReadingScoresHigherThanGarbled) {
    CHECK(OcrNoise::score("CESARE: Wait for me!") > OcrNoise::score("- CESAREiVJütt'h!"));
    CHECK(OcrNoise::score("We go to McDonald's now.") > 0);
    CHECK(OcrNoise::score("T hats the[lil Should be quiet now.") < OcrNoise::score("That should be quiet now."));
}

TEST(OcrNoise_GarbledReadingsAreRecognised) {
    CHECK(OcrNoise::looksGarbled("Gtt solkolfege."));
    CHECK(OcrNoise::looksGarbled("- CESAREiVJütt'h!"));
    CHECK(OcrNoise::looksGarbled("Wrrk thng."));
    CHECK(!OcrNoise::looksGarbled("Hold the gate."));
    CHECK(!OcrNoise::looksGarbled("The FBI and the NPC agents."));
    CHECK(!OcrNoise::looksGarbled("Hmm, I don't know about that, boy."));
    CHECK(!OcrNoise::looksGarbled("Go to college."));
}

// From SubtitleBlockTests.cs (OcrNoise part).
TEST(OcrNoise_GaugeNumberStuckToLineIsDropped) {
    const char* cases[][2] = {
        {"9? We'll lose these tracks.", "We'll lose these tracks."},
        {"100. Mac and John...", "Mac and John..."},
        {"23m CESARE: It's here! Shoot him!", "CESARE: It's here! Shoot him!"},
        {"100 men came", "100 men came"},
    };
    for (auto& c : cases) CHECK_EQ(OcrNoise::clean(c[0]), std::string(c[1]));
}
