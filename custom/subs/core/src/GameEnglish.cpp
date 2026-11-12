// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/GameEnglish.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: each .NET pattern \b(words)\b (IgnoreCase, CultureInvariant) is a list of literal
// alternatives matched case-insensitively at word boundaries (\w as in .NET).
#include "crispplay/subs/GameEnglish.h"

#include "crispplay/subs/Unicode.h"

#include <vector>

namespace crispplay::subs::GameEnglish {
namespace {

struct Phrase {
    std::vector<std::u32string_view> alternatives;  // tried in order, like a regex alternation
    std::u32string_view plain;
};

const Phrase kPhrases[] = {
    {{U"friendlies"}, U"allies"},
    {{U"I've got your six", U"I got your six"}, U"I've got your back"},
    {{U"check your six"}, U"watch behind you"},
    {{U"heads up"}, U"watch out"},
    {{U"took you long enough"}, U"you're late"},
    {{U"we've got company"}, U"enemies are coming"},
    {{U"lock and load"}, U"get ready"},
    {{U"sitting ducks"}, U"easy targets"},
    {{U"moving in"}, U"going in"},
};

bool boundary(const std::u32string& s, size_t i) {
    const bool before = i > 0 && uni::isWordChar(s[i - 1]);
    const bool after = i < s.size() && uni::isWordChar(s[i]);
    return before != after;
}

std::u32string replace(const std::u32string& text, const Phrase& phrase) {
    std::u32string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t matched = 0;
        if (boundary(text, i)) {
            for (auto alt : phrase.alternatives) {
                if (i + alt.size() <= text.size() && uni::equalsIgnoreCase(std::u32string_view(text).substr(i, alt.size()), alt) &&
                    boundary(text, i + alt.size())) {
                    matched = alt.size();
                    break;
                }
            }
        }
        if (matched == 0) {
            out.push_back(text[i++]);
            continue;
        }
        std::u32string plain(phrase.plain);
        if (uni::isUpper(text[i])) plain[0] = uni::toUpperInvariant(plain[0]);
        out += plain;
        i += matched;
    }
    return out;
}

}  // namespace

std::string plain(const std::string& sentence) {
    auto text = uni::decode(sentence);
    for (const auto& phrase : kPhrases) text = replace(text, phrase);
    return uni::encode(text);
}

}  // namespace crispplay::subs::GameEnglish
