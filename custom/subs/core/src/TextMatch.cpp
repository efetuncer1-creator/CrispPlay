// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TextMatch.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/TextMatch.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <vector>

namespace crispplay::subs::TextMatch {
namespace {

using Words = std::vector<std::u32string>;

bool isAlphanumeric(char32_t c) { return uni::isLetter(c) || uni::isMark(c) || uni::isNumber(c); }

std::u32string normalize32(std::u32string_view s) {
    std::u32string spaced;
    spaced.reserve(s.size());
    for (char32_t c : s) {
        c = uni::toLowerInvariant(c);
        spaced.push_back(isAlphanumeric(c) ? c : U' ');
    }
    return uni::join(uni::splitWhiteSpace(spaced), U" ");
}

template <typename Seq>
double ratio(const Seq& a, const Seq& b) {
    std::vector<int> previous(b.size() + 1), row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) previous[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        row[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            row[j] = std::min({previous[j] + 1, row[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(previous, row);
    }
    return 1.0 - static_cast<double>(previous[b.size()]) / static_cast<double>(std::max(a.size(), b.size()));
}

bool isSimilar32(const std::u32string& a, const std::u32string& b, double threshold) {
    if (a == b) return !a.empty();
    if (a.empty() || b.empty()) return false;
    return ratio(a, b) >= threshold;
}

// Single letters ("a", "I" or an invented "O") and words with digits add nothing.
Words cleanWords(std::u32string_view normalized) {
    Words out;
    for (auto& w : uni::splitWhiteSpace(normalized))
        if (w.size() > 1 && std::none_of(w.begin(), w.end(), [](char32_t c) { return uni::isNumber(c); }))
            out.push_back(std::move(w));
    return out;
}

double similarity(const Words& x, const Words& y) {
    if (x.empty() || y.empty()) return x.empty() && y.empty() ? 1 : 0;
    return ratio(x, y);
}

bool startsWith(const Words& words, const Words& part) {
    return part.size() <= words.size() && std::equal(part.begin(), part.end(), words.begin());
}
bool endsWith(const Words& words, const Words& part) {
    return part.size() <= words.size() && std::equal(part.begin(), part.end(), words.end() - part.size());
}

// Do `count` words from the start (or end) make whole rows?
bool isWholeRows(size_t count, const std::vector<std::u32string>& rows, bool fromStart) {
    size_t total = 0;
    auto step = [&](const std::u32string& row) {
        total += cleanWords(row).size();
        if (total == count) return 1;
        if (total > count) return -1;
        return 0;
    };
    if (fromStart) {
        for (const auto& row : rows)
            if (int r = step(row)) return r > 0;
    } else {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            if (int r = step(*it)) return r > 0;
    }
    return false;
}

bool isGrowth32(std::u32string_view newText, std::u32string_view oldText) {
    auto x = uni::splitWhiteSpace(normalize32(newText));
    auto y = uni::splitWhiteSpace(normalize32(oldText));
    if (y.empty() || x.size() <= y.size()) return false;
    return startsWith(x, y) || (y.size() >= 2 && endsWith(x, y));
}

// Removes one occurrence of `word`; true if it was there (List.Remove).
bool removeOne(Words& words, const std::u32string& word) {
    auto it = std::find(words.begin(), words.end(), word);
    if (it == words.end()) return false;
    words.erase(it);
    return true;
}

}  // namespace

std::string normalize(const std::string& s) { return uni::encode(normalize32(uni::decode(s))); }

bool isSimilar(const std::string& a, const std::string& b, double threshold) {
    return isSimilar32(uni::decode(a), uni::decode(b), threshold);
}

std::string normalizeRows(const std::string& s) {
    auto text = uni::replaceAll(uni::decode(s), U"\r\n", U"\n");
    std::vector<std::u32string> rows;
    for (const auto& row : uni::split(text, U'\n')) {
        auto n = normalize32(row);
        if (!n.empty()) rows.push_back(std::move(n));
    }
    return uni::encode(uni::join(rows, U"\n"));
}

bool isSameSubtitle(const std::string& rereadUtf8, const std::string& sentUtf8) {
    const auto reread = uni::decode(rereadUtf8);
    const auto rows = uni::split(uni::decode(sentUtf8), U'\n');
    const auto flat = uni::join(rows, U" ");
    const auto x = cleanWords(reread);
    const auto y = cleanWords(flat);
    // Grown text (typewriter effect, or a new line starting with the same words) is new.
    if (!y.empty() && x.size() > y.size() && (startsWith(x, y) || endsWith(x, y))) return false;
    if (isSimilar32(reread, flat, 0.85)) return true;
    // Cut at the start or end: same subtitle only if whole rows were read.
    if (!x.empty() && x.size() < y.size() && (startsWith(y, x) || endsWith(y, x)))
        return isWholeRows(x.size(), rows, startsWith(y, x));
    auto remaining = y;
    const size_t sentCount = remaining.size();
    size_t shared = 0;
    for (const auto& word : x)
        if (removeOne(remaining, word)) ++shared;
    // Garbage words dropped from between: nearly all words of the reread are in the sent line.
    if (x.size() >= 4 && x.size() <= sentCount && shared >= x.size() * 0.85) return true;
    // In a short line one word changes the meaning; there only character similarity counts.
    const size_t longer = std::max(x.size(), sentCount), shorter = std::min(x.size(), sentCount);
    if (shorter < 6 || static_cast<double>(shorter) / longer < 0.75) return false;
    return static_cast<double>(shared) / longer >= 0.75;
}

bool isRevision(const std::string& newText, const std::string& oldText) {
    const auto n32 = uni::decode(newText), o32 = uni::decode(oldText);
    const auto x = normalize32(n32);
    const auto y = normalize32(o32);
    if (x.empty() || y.empty()) return false;
    if (isGrowth32(n32, o32) || isGrowth32(o32, n32)) return true;
    if (isSimilar32(x, y, 0.8)) return true;
    auto a = cleanWords(x);
    auto b = cleanWords(y);
    const size_t shorter = std::min(a.size(), b.size()), longer = std::max(a.size(), b.size());
    if (shorter < 5) return false;
    if (similarity(a, b) >= 0.65) return true;
    size_t shared = 0;
    for (const auto& word : a)
        if (removeOne(b, word)) ++shared;
    return static_cast<double>(shared) / longer >= 0.7;
}

bool isWorseReread(const std::string& reread, const std::string& sent) {
    const auto r32 = uni::decode(reread), s32 = uni::decode(sent);
    const auto a = normalize32(r32);
    const auto b = normalize32(s32);
    const auto x = cleanWords(a);
    const auto y = cleanWords(b);
    if (x.size() < 5 || y.empty() || isGrowth32(r32, s32) || isGrowth32(s32, r32)) return false;
    const bool reads = similarity(x, y) >= 0.65 || isSimilar32(a, b, 0.75);
    return reads && OcrNoise::score(reread) < OcrNoise::score(sent) + 4;
}

bool isGrowth(const std::string& newText, const std::string& oldText) {
    return isGrowth32(uni::decode(newText), uni::decode(oldText));
}

}  // namespace crispplay::subs::TextMatch
