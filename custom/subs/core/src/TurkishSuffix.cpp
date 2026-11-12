// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TurkishSuffix.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/TurkishSuffix.h"

#include "crispplay/subs/Unicode.h"

#include <algorithm>

namespace crispplay::subs::TurkishSuffix {
namespace {

using uni::contains;
constexpr std::u32string_view kVowels = U"aeıioöuü";
constexpr std::u32string_view kBackVowels = U"aıou";
constexpr std::u32string_view kRoundedVowels = U"ouöü";
constexpr std::u32string_view kHighVowels = U"ıiuü";
constexpr std::u32string_view kVoiceless = U"fstkçşhp";
// A longer "suffix" is most likely not a suffix; left alone.
constexpr size_t kMaxSuffixLength = 12;

bool isVowel(char32_t c) { return c != 0 && contains(kVowels, c); }
bool isHigh(char32_t c) { return c != 0 && contains(kHighVowels, c); }

// a/e and ı/i/u/ü of the suffix follow the previous vowel; a final "-ki" (Midgard'daki) stays.
std::u32string harmonize(const std::u32string& body, char32_t lastVowel) {
    std::u32string out;
    char32_t previous = lastVowel;
    for (size_t i = 0; i < body.size(); ++i) {
        char32_t c = body[i];
        const bool invariantKi = c == U'i' && i > 0 && body[i - 1] == U'k' && i == body.size() - 1;
        if (!invariantKi && (c == U'a' || c == U'e')) {
            c = contains(kBackVowels, previous) ? U'a' : U'e';
        } else if (!invariantKi && isHigh(c)) {
            const bool back = contains(kBackVowels, previous);
            const bool rounded = contains(kRoundedVowels, previous);
            c = back ? (rounded ? U'u' : U'ı') : (rounded ? U'ü' : U'i');
        }
        if (isVowel(c)) previous = c;
        out.push_back(c);
    }
    return out;
}

// A common noun's final p/ç/k softens before a vowel suffix (kitabı, kılıcı, bıçağı, rengi);
// one-syllable words do not (topu); t is left alone.
std::u32string soften(const std::u32string& term) {
    if (term.size() < 2) return term;
    const char32_t last = term.back();
    const auto lower = uni::toLowerTurkish(term);
    const size_t space = lower.rfind(U' ');
    const auto word = lower.substr(space == std::u32string::npos ? 0 : space + 1);
    const auto stem = term.substr(0, term.size() - 1);
    if (uni::endsWith(word, U"nk")) return stem + U"g";
    if (std::count_if(word.begin(), word.end(), isVowel) < 2) return term;
    switch (last) {
    case U'p': return stem + U"b";
    case U'ç': return stem + U"c";
    case U'k': return stem + U"ğ";
    default: return term;
    }
}

}  // namespace

std::u32string attach(const std::u32string& term, const std::u32string& suffix, char32_t apostrophe) {
    if (term.empty() || suffix.empty()) return term;
    auto body = uni::toLowerTurkish(suffix);
    if (body.size() > kMaxSuffixLength || !std::all_of(body.begin(), body.end(), [](char32_t c) { return uni::isLetter(c); }))
        return term + apostrophe + suffix;

    const bool proper = uni::isUpper(term[0]);
    const size_t space = term.rfind(U' ');
    const auto lastWord = uni::toLowerTurkish(term.substr(space == std::u32string::npos ? 0 : space + 1));
    char32_t lastLetter = 0, lastVowel = 0;
    for (char32_t c : lastWord) {
        if (uni::isLetter(c)) lastLetter = c;
        if (isVowel(c)) lastVowel = c;
    }
    // A vowelless abbreviation ("XP", "HP") is read by letter names: front, ends in a vowel.
    const bool endsWithVowel = lastVowel == 0 || isVowel(lastLetter);
    if (lastVowel == 0) lastVowel = U'e';
    const bool compound = space != std::u32string::npos && isHigh(lastLetter) && lastWord.size() >= 3;
    const bool voiceless = !endsWithVowel && lastLetter != 0 && contains(kVoiceless, lastLetter);

    // A leading buffer letter was for the placeholder; dropped.
    if (body.size() >= 2 && body[0] == U'y' && (isVowel(body[1]) || body[1] == U'l')) body = body.substr(1);
    else if (body.size() >= 3 && body[0] == U'n' && isHigh(body[1]) && body[2] == U'n') body = body.substr(1);

    const bool genitive = body.size() >= 2 && isHigh(body[0]) && body[1] == U'n' && (body.size() == 2 || !isVowel(body[2]));
    const bool instrumental = body.size() >= 2 && body[0] == U'l' && (body[1] == U'a' || body[1] == U'e') &&
                              !(body.size() >= 3 && body[2] == U'r');
    std::u32string buffer;
    if (isVowel(body[0])) {
        if (compound || (endsWithVowel && genitive)) buffer = U"n";
        else if (endsWithVowel) buffer = U"y";
    } else if (body[0] == U'd' || body[0] == U't') {
        if (compound) buffer = U"n";
        body[0] = (compound || !voiceless) ? U'd' : U't';
    } else if (instrumental && (endsWithVowel || compound)) {
        buffer = U"y";
    }

    const auto harmonized = harmonize(body, lastVowel);
    if (proper) return term + apostrophe + buffer + harmonized;
    const auto stem = buffer.empty() && isVowel(harmonized[0]) ? soften(term) : term;
    return stem + buffer + harmonized;
}

std::string attach(const std::string& term, const std::string& suffix, char32_t apostrophe) {
    return uni::encode(attach(uni::decode(term), uni::decode(suffix), apostrophe));
}

}  // namespace crispplay::subs::TurkishSuffix
