// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TermProtection.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: the .NET pattern (?<![\p{L}\p{N}])NAME(?:\s?['’`´](\p{L}+)|(\p{L}+))?(?![\p{L}\p{N}])
// (IgnoreCase) is a hand-written matcher with the same alternation order.
#include "crispplay/subs/TermProtection.h"

#include "crispplay/subs/TurkishSuffix.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>

namespace crispplay::subs {
namespace {

bool letterOrNumber(char32_t c) { return uni::isLetter(c) || uni::isNumber(c); }

struct Hit {
    size_t start = 0, end = 0;
    std::u32string suffix;
};

size_t lettersFrom(const std::u32string& s, size_t i) {
    size_t j = i;
    while (j < s.size() && uni::isLetter(s[j])) ++j;
    return j;
}

std::optional<Hit> matchAt(const std::u32string& s, size_t i, const std::u32string& name) {
    if (i + name.size() > s.size() || !uni::equalsIgnoreCase(std::u32string_view(s).substr(i, name.size()), name)) return std::nullopt;
    if (i > 0 && letterOrNumber(s[i - 1])) return std::nullopt;
    const size_t j = i + name.size();
    auto endsWell = [&](size_t k) { return k >= s.size() || !letterOrNumber(s[k]); };
    // \s?['’`´](\p{L}+)
    for (int space = 1; space >= 0; --space) {
        size_t k = j;
        if (space) {
            if (k < s.size() && uni::isWhiteSpace(s[k])) ++k;
            else continue;
        }
        if (k < s.size() && uni::contains(U"'’`´", s[k])) {
            const size_t end = lettersFrom(s, k + 1);
            if (end > k + 1 && endsWell(end)) return Hit{i, end, s.substr(k + 1, end - k - 1)};
        }
    }
    // (\p{L}+)
    const size_t end = lettersFrom(s, j);
    if (end > j && endsWell(end)) return Hit{i, end, s.substr(j, end - j)};
    if (endsWell(j)) return Hit{i, j, {}};
    return std::nullopt;
}

std::vector<Hit> findAll(const std::u32string& s, const std::u32string& name) {
    std::vector<Hit> hits;
    size_t i = 0;
    while (i < s.size()) {
        if (auto hit = matchAt(s, i, name)) {
            i = std::max(hit->end, i + 1);
            hits.push_back(std::move(*hit));
        } else {
            ++i;
        }
    }
    return hits;
}

// Start of the text or of a sentence (only spaces, quotes, dashes or a sentence end before).
bool startsSentence(const std::u32string& text, size_t index) {
    for (size_t i = index; i-- > 0;) {
        const char32_t c = text[i];
        if (uni::isWhiteSpace(c) || uni::contains(U"\"“'‘(-–—¡¿", c)) continue;
        return uni::contains(U".!?…", c);
    }
    return true;
}

std::u32string capitalize(const std::u32string& term) {
    if (term.empty() || !uni::isLower(term[0])) return term;
    return uni::toUpperTurkish(term[0]) + term.substr(1);
}

}  // namespace

const std::vector<std::string>& TermProtector::placeholders() {
    // Not words in Turkish or English, not prefixes of each other, no i/I (Turkish casing),
    // end in a consonant (no softening) and have back vowels.
    static const std::vector<std::string> names = {"Zarmon", "Velkor", "Dorvan", "Kelmar", "Tolvar", "Brandur", "Halvor", "Feldor"};
    return names;
}

TermProtector::TermProtector(const Glossary& glossary, const std::vector<std::string>& sources) : glossary_(glossary) {
    for (const auto& p : placeholders()) {
        const auto name = uni::decode(p);
        const bool used = std::any_of(sources.begin(), sources.end(), [&](const std::string& s) {
            return uni::findIgnoreCase(uni::decode(s), name) != std::u32string::npos;
        });
        if (!used) free_.push_back(p);
    }
}

TermProtector::Protected TermProtector::protect(const std::string& textUtf8) {
    const auto text = uni::decode(textUtf8);
    const auto matches = glossary_.find(text);
    if (matches.empty()) return {textUtf8, {}};
    std::u32string out;
    std::vector<Slot> used;
    size_t position = 0;
    for (const auto& match : matches) {
        const auto key = uni::toLowerInvariant(match.english);
        auto it = std::find_if(byTerm_.begin(), byTerm_.end(), [&](const auto& e) { return e.first == key; });
        const Slot* slot = nullptr;
        if (it != byTerm_.end()) {
            slot = &it->second;
        } else {
            // Out of names: the remaining terms are not protected; the model translates them.
            if (free_.empty()) continue;
            byTerm_.emplace_back(key, Slot{free_.front(), uni::encode(match.english), uni::encode(match.turkish)});
            all_.push_back(byTerm_.back().second);
            free_.erase(free_.begin());
            slot = &byTerm_.back().second;
        }
        out += text.substr(position, match.start - position);
        out += uni::decode(slot->placeholder);
        position = match.start + match.length;
        if (std::find(used.begin(), used.end(), *slot) == used.end()) used.push_back(*slot);
    }
    out += text.substr(position);
    return {uni::encode(out), used};
}

std::optional<std::string> TermProtector::restore(const std::string& outputUtf8, const std::vector<Slot>& expected,
                                                  const std::vector<Slot>& all, char32_t apostrophe) {
    if (expected.empty() && all.empty()) return outputUtf8;
    auto output = uni::decode(outputUtf8);
    for (const auto& slot : all)
        if (std::find(expected.begin(), expected.end(), slot) == expected.end() && !findAll(output, uni::decode(slot.placeholder)).empty())
            return std::nullopt;
    for (const auto& slot : expected) {
        const auto hits = findAll(output, uni::decode(slot.placeholder));
        if (hits.empty()) return std::nullopt;
        const auto turkish = uni::decode(slot.turkish);
        std::u32string next;
        size_t position = 0;
        for (const auto& hit : hits) {
            const auto term = TurkishSuffix::attach(turkish, hit.suffix, apostrophe);
            next += output.substr(position, hit.start - position);
            next += startsSentence(output, hit.start) ? capitalize(term) : term;
            position = hit.end;
        }
        next += output.substr(position);
        output = std::move(next);
    }
    return uni::encode(output);
}

}  // namespace crispplay::subs
