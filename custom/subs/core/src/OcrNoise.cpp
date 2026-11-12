// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/OcrNoise.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: the two .NET regular expressions (LeadingNumber, Usual) are hand-written matchers;
// \d is Unicode Nd as in .NET.
#include "crispplay/subs/OcrNoise.h"

#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace crispplay::subs::OcrNoise {
namespace {

using uni::contains;
const std::u32string_view kSymbols = U"$@#%^*_=|\\~<>{}¢£¥§©®°±µ¶¤";

struct Parts { std::u32string lead, core, trail; };

bool isEdge(char32_t c) {
    return !contains(kSymbols, c) &&
           (uni::isPunctuation(c) || uni::isSymbol(c) || contains(U"\"'“”‘’«»()[]", c));
}

Parts splitToken(std::u32string_view token) {
    size_t start = 0, end = token.size();
    while (start < end && isEdge(token[start])) ++start;
    while (end > start && isEdge(token[end - 1])) --end;
    return {std::u32string(token.substr(0, start)), std::u32string(token.substr(start, end - start)),
            std::u32string(token.substr(end))};
}

bool isAsciiLetter(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

size_t digitRun(std::u32string_view s, size_t i) {
    size_t j = i;
    while (j < s.size() && uni::isDigit(s[j])) ++j;
    return j - i;
}

// ^(?:[A-Za-z]{1,4}\d{1,4}|\d+(?:st|nd|...)|[xX]\d{1,3}|\d+[.,]\d+[A-Za-z]{0,3}|\d{1,2}[:.]\d{2}(?:am|pm|AM|PM)?)$
bool usual(std::u32string_view s) {
    // [A-Za-z]{1,4}\d{1,4}
    {
        size_t i = 0;
        while (i < s.size() && isAsciiLetter(s[i])) ++i;
        const size_t d = digitRun(s, i);
        if (i >= 1 && i <= 4 && d >= 1 && d <= 4 && i + d == s.size()) return true;
    }
    const size_t lead = digitRun(s, 0);
    // \d+(?:suffix)
    if (lead > 0) {
        static const std::u32string_view kSuffixes[] = {U"st", U"nd", U"rd", U"th", U"s", U"k", U"K", U"m", U"km", U"kg",
                                                        U"cm", U"mm", U"p", U"x", U"fps", U"hp", U"HP", U"XP", U"xp",
                                                        U"v", U"V", U"D", U"d"};
        const auto rest = s.substr(lead);
        for (auto suffix : kSuffixes)
            if (rest == suffix) return true;
    }
    // [xX]\d{1,3}
    if (!s.empty() && (s[0] == 'x' || s[0] == 'X')) {
        const size_t d = digitRun(s, 1);
        if (d >= 1 && d <= 3 && 1 + d == s.size()) return true;
    }
    // \d+[.,]\d+[A-Za-z]{0,3}
    if (lead > 0 && lead < s.size() && (s[lead] == '.' || s[lead] == ',')) {
        const size_t d = digitRun(s, lead + 1);
        if (d > 0) {
            size_t i = lead + 1 + d, letters = 0;
            while (i < s.size() && isAsciiLetter(s[i])) ++i, ++letters;
            if (i == s.size() && letters <= 3) return true;
        }
    }
    // \d{1,2}[:.]\d{2}(?:am|pm|AM|PM)?
    if (lead >= 1 && lead <= 2 && lead < s.size() && (s[lead] == ':' || s[lead] == '.')) {
        const size_t d = digitRun(s, lead + 1);
        if (d == 2) {
            const auto rest = s.substr(lead + 3);
            if (rest.empty() || rest == U"am" || rest == U"pm" || rest == U"AM" || rest == U"PM") return true;
        }
    }
    return false;
}

// ^\d+(?:m|km|ft|yd)?[.?!:;]?$
bool leadingNumber(std::u32string_view s) {
    size_t i = digitRun(s, 0);
    if (i == 0) return false;
    for (std::u32string_view unit : {U"km", U"ft", U"yd", U"m"})
        if (s.substr(i, unit.size()) == unit) { i += unit.size(); break; }
    if (i < s.size() && contains(U".?!:;", s[i])) ++i;
    return i == s.size();
}

// "PS5", "R2", "MK2", "2nd", "100m", "4K", "60fps", "x2", "1980s", "R2-D2".
bool isUsualMix(std::u32string_view core) {
    for (const auto& part : uni::split(core, U'-')) {
        const bool hasNumber = std::any_of(part.begin(), part.end(), [](char32_t c) { return uni::isNumber(c); });
        if (hasNumber && !usual(part)) return false;
    }
    return true;
}

// The word itself; nullopt if it is all garbage.
std::optional<std::u32string> cleanCore(const std::u32string& core) {
    const auto letters = std::count_if(core.begin(), core.end(), [](char32_t c) { return uni::isLetter(c); });
    const auto digits = std::count_if(core.begin(), core.end(), [](char32_t c) { return uni::isNumber(c); });
    const bool hasSymbol = std::any_of(core.begin(), core.end(), [](char32_t c) { return contains(kSymbols, c); });
    if (letters == 0) return core;  // "24", "$500", "…"
    if (!hasSymbol && digits == 0) return core;
    if (!hasSymbol && isUsualMix(core)) return core;
    // Garbage glued to a real word: at least 3 leading letters stay ("waters2aljd" -> "waters").
    std::u32string prefix;
    for (char32_t c : core) {
        if (!(uni::isLetter(c) || c == '\'' || c == U'’' || c == '-')) break;
        prefix.push_back(c);
    }
    if (std::count_if(prefix.begin(), prefix.end(), [](char32_t c) { return uni::isLetter(c); }) >= 3) return prefix;
    return std::nullopt;
}

// "word", "WORD", "Word", "McDonald", "don't": capitals only first or all.
bool normalCase(const std::u32string& core) {
    std::u32string letters;
    for (char32_t c : core)
        if (uni::isLetter(c)) letters.push_back(c);
    const bool allUpper = std::all_of(letters.begin(), letters.end(), [](char32_t c) { return uni::isUpper(c); });
    const bool restLower = letters.empty() ||
                           std::all_of(letters.begin() + 1, letters.end(), [](char32_t c) { return uni::isLower(c); });
    if (allUpper || restLower) return true;
    const auto innerUpper = std::count_if(letters.begin() + 1, letters.end(), [](char32_t c) { return uni::isUpper(c); });
    const auto lower = std::count_if(letters.begin(), letters.end(), [](char32_t c) { return uni::isLower(c); });
    return innerUpper == 1 && lower >= 2;
}

bool hasBracket(const std::u32string& core) {
    return std::any_of(core.begin(), core.end(), [](char32_t c) { return c == '[' || c == ']' || c == '(' || c == ')'; });
}

// Three or more letters, no vowel, not an all-caps abbreviation: not English ("Gtt", "rnnk").
bool vowelless(const std::u32string& letters) {
    if (letters.size() < 3) return false;
    if (std::all_of(letters.begin(), letters.end(), [](char32_t c) { return uni::isUpper(c); })) return false;
    const auto lower = uni::toLowerInvariant(letters);
    return lower.find_first_of(U"aeiouyäöüıàáâèéêìíîòóôùúû") == std::u32string::npos;
}

// "l", "|", "l'm", "|'ll" -> "I", "I'm", "I'll".
std::optional<std::u32string> misreadI(const std::u32string& core) {
    if (core.empty() || (core[0] != 'l' && core[0] != '|')) return std::nullopt;
    if (core.size() == 1) return U"I";
    if (core[1] != '\'' && core[1] != U'’') return std::nullopt;
    const auto rest = core.substr(2);
    if (rest != U"m" && rest != U"ll" && rest != U"ve" && rest != U"d") return std::nullopt;
    return U"I" + core.substr(1);
}

// Only ? ! . , ; : marks; an ellipsis ("...") is real punctuation.
bool isStrayPunctuation(const std::u32string& token) {
    if (token.empty()) return false;
    if (!std::all_of(token.begin(), token.end(), [](char32_t c) { return contains(U"?!.,;:", c); })) return false;
    return !(token.size() >= 2 && std::all_of(token.begin(), token.end(), [](char32_t c) { return c == '.'; }));
}

}  // namespace

std::string clean(const std::string& line) {
    std::vector<std::u32string> kept;
    const auto tokens = uni::splitWhiteSpace(uni::decode(line));
    for (size_t index = 0; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        const auto [lead, core, trail] = splitToken(token);
        // A lone "l" or "|" is usually a misread "I"; unless a lower case word follows, it is the
        // edge of the subtitle box ("| Get in the car.") and dropped.
        if (auto pronoun = misreadI(core)) {
            char32_t next = 0;
            if (index + 1 < tokens.size())
                for (char32_t c : tokens[index + 1])
                    if (uni::isLetter(c)) { next = c; break; }
            if (*pronoun != U"I" || uni::isLower(next)) kept.push_back(lead + *pronoun + trail);
            continue;
        }
        // Marks alone ("?! Where", "car. ?!"): invented at line start or after a sentence end;
        // detached from a word they stick to it ("going ?!").
        if (core.empty() && isStrayPunctuation(token)) {
            if (!kept.empty() && !contains(U".?!…,;:", kept.back().back())) kept.back() += token;
            continue;
        }
        if (auto fixed = cleanCore(core))
            kept.push_back(lead + *fixed + trail);
        else if (!trail.empty() && !kept.empty())
            kept.back() += trail;  // punctuation after dropped garbage belongs to the sentence
    }
    // A gauge number stuck to the line start ("9? We'll lose", "23m CESARE: Here!") is dropped.
    if (kept.size() >= 3 && leadingNumber(kept[0]) && uni::isUpper(kept[1][0])) kept.erase(kept.begin());
    return uni::encode(uni::join(kept, U" "));
}

int score(const std::string& text) {
    int total = 0;
    for (const auto& token : uni::splitWhiteSpace(uni::decode(text))) {
        const auto core = splitToken(token).core;
        const int letters = static_cast<int>(std::count_if(core.begin(), core.end(), [](char32_t c) { return uni::isLetter(c); }));
        if (letters == 0) continue;
        const auto cleaned = cleanCore(core);
        const bool isClean = cleaned && *cleaned == core && normalCase(core) && !hasBracket(core);
        total += isClean ? letters : -2 * letters;
    }
    return total;
}

bool looksGarbled(const std::string& text) {
    int good = 0, bad = 0;
    for (const auto& token : uni::splitWhiteSpace(uni::decode(text))) {
        const auto core = splitToken(token).core;
        std::u32string letters;
        for (char32_t c : core)
            if (uni::isLetter(c)) letters.push_back(c);
        if (letters.empty()) continue;
        const auto cleaned = cleanCore(core);
        const bool isClean = cleaned && *cleaned == core && normalCase(core) && !hasBracket(core) && !vowelless(letters);
        (isClean ? good : bad) += static_cast<int>(letters.size());
    }
    return bad * 6 > good + bad;
}

}  // namespace crispplay::subs::OcrNoise
