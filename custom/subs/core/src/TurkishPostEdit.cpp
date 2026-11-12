// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TurkishPostEdit.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: the regular expressions (space before punctuation, repeated spaces, imperative
// pattern (?<!\p{L})polite(?=\s*[!.,?…]|\s*$), word split [^\p{L}]+) are hand-written scanners.
// Space class [ \t ] as in the original.
#include "crispplay/subs/TurkishPostEdit.h"

#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <tuple>

namespace crispplay::subs::TurkishPostEdit {
namespace {

using uni::contains;

bool isSpace(char32_t c) { return c == U' ' || c == U'\t' || c == 0xA0; }

std::u32string removeSpaceBeforePunctuation(const std::u32string& s) {
    std::u32string out;
    size_t i = 0;
    while (i < s.size()) {
        if (isSpace(s[i])) {
            size_t j = i;
            while (j < s.size() && isSpace(s[j])) ++j;
            if (j < s.size() && contains(U",.!?;:…", s[j])) { i = j; continue; }
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

std::u32string collapseSpaces(const std::u32string& s) {
    std::u32string out;
    size_t i = 0;
    while (i < s.size()) {
        if (isSpace(s[i])) {
            size_t j = i;
            while (j < s.size() && isSpace(s[j])) ++j;
            if (j - i >= 2) out.push_back(U' ');
            else out.push_back(s[i]);
            i = j;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

bool isExpressive(const std::u32string& end) {
    return !end.empty() && (contains(end, U'!') || contains(end, U'?') || contains(end, U'…') || end == U"...");
}

// text = body + final punctuation (. ! ? …) + closing quotes/brackets
std::tuple<std::u32string, std::u32string, std::u32string> splitEnd(std::u32string text) {
    text = uni::trimEnd(text);
    size_t closerStart = text.size();
    while (closerStart > 0 && contains(U"\"'”’)]»", text[closerStart - 1])) --closerStart;
    size_t endStart = closerStart;
    while (endStart > 0 && contains(U".!?…", text[endStart - 1])) --endStart;
    return {text.substr(0, endStart), text.substr(endStart, closerStart - endStart), text.substr(closerStart)};
}

std::u32string ending32(const std::u32string& source, const std::u32string& translation) {
    const auto [sb, sourceEnd, sourceClosers] = splitEnd(source);
    if (!isExpressive(sourceEnd)) return translation;
    const auto [body, end, closers] = splitEnd(translation);
    // Final mark inside quotes in the source (He said "Run!"): left alone.
    if (sourceClosers.empty() != closers.empty()) return translation;
    if (end == sourceEnd || !(end.empty() || end == U"." || isExpressive(end))) return translation;
    if (!uni::anyLetter(body)) return translation;
    return body + sourceEnd + closers;
}

std::u32string quotes32(const std::u32string& source, const std::u32string& translation) {
    const bool curlyDouble = contains(source, U'“') || contains(source, U'”');
    const bool curlySingle = contains(source, U'’') || contains(source, U'‘');
    std::u32string out;
    for (size_t i = 0; i < translation.size(); ++i) {
        const char32_t c = translation[i];
        const bool opening = i == 0 || uni::isWhiteSpace(translation[i - 1]) || contains(U"([-–—", translation[i - 1]);
        char32_t r = c;
        if ((c == U'“' || c == U'”' || c == U'„' || c == U'‟') && !curlyDouble) r = U'"';
        else if (c == U'"' && curlyDouble) r = opening ? U'“' : U'”';
        else if ((c == U'’' || c == U'‘') && !curlySingle) r = U'\'';
        else if (c == U'\'' && curlySingle) r = opening ? U'‘' : U'’';
        out.push_back(r);
    }
    return out;
}

const std::u32string_view kSenWords[] = {U"sen", U"seni", U"sana", U"senin", U"sende", U"senden", U"seninle", U"senle"};
const std::u32string_view kSizWords[] = {U"siz", U"sizi", U"size", U"sizin", U"sizde", U"sizden", U"sizinle", U"sizler"};

// "siz" imperative -> "sen" imperative; words that may also be plural or polite are left out.
const std::pair<std::u32string_view, std::u32string_view> kImperatives[] = {
    {U"dikkat edin", U"dikkat et"}, {U"acele edin", U"acele et"}, {U"ateş edin", U"ateş et"}, {U"yardım edin", U"yardım et"},
    {U"takip edin", U"takip et"}, {U"sessiz olun", U"sessiz ol"}, {U"hazır olun", U"hazır ol"}, {U"sakin olun", U"sakin ol"},
    {U"bekleyin", U"bekle"}, {U"durun", U"dur"}, {U"gelin", U"gel"}, {U"koşun", U"koş"}, {U"gidin", U"git"}, {U"bakın", U"bak"},
    {U"dinleyin", U"dinle"}, {U"saklanın", U"saklan"}, {U"kaçın", U"kaç"}, {U"savaşın", U"savaş"}, {U"dövüşün", U"dövüş"},
    {U"durdurun", U"durdur"}, {U"çekilin", U"çekil"}, {U"eğilin", U"eğil"}, {U"atlayın", U"atla"}, {U"tutun", U"tut"},
    {U"bırakın", U"bırak"}, {U"söyleyin", U"söyle"}, {U"gösterin", U"göster"}, {U"getirin", U"getir"}, {U"kalkın", U"kalk"},
    {U"odaklanın", U"odaklan"}, {U"yaklaşın", U"yaklaş"}, {U"uzaklaşın", U"uzaklaş"},
};

bool imperativePlace(const std::u32string& s, size_t end) {
    size_t k = end;
    while (k < s.size() && uni::isWhiteSpace(s[k])) ++k;
    if (k == s.size()) return true;
    if (contains(U"!.,?…", s[k])) return true;
    // \s*$ also matches before a final line feed.
    return s[k] == U'\n' && k + 1 == s.size();
}

std::u32string informal(std::u32string sentence) {
    for (const auto& [polite, inf] : kImperatives) {
        std::u32string out;
        size_t i = 0;
        while (i < sentence.size()) {
            const bool hit = i + polite.size() <= sentence.size() &&
                             uni::equalsIgnoreCase(std::u32string_view(sentence).substr(i, polite.size()), polite) &&
                             (i == 0 || !uni::isLetter(sentence[i - 1])) && imperativePlace(sentence, i + polite.size());
            if (!hit) {
                out.push_back(sentence[i++]);
                continue;
            }
            std::u32string replacement(inf);
            if (uni::isUpper(sentence[i])) replacement[0] = uni::toUpperTurkish(replacement[0]);
            out += replacement;
            i += polite.size();
        }
        sentence = std::move(out);
    }
    return sentence;
}

bool containsWord(const std::u32string& text, const std::u32string_view* begin, const std::u32string_view* end) {
    const auto lower = uni::toLowerTurkish(text);
    std::u32string token;
    auto check = [&] { return !token.empty() && std::find(begin, end, std::u32string_view(token)) != end; };
    for (char32_t c : lower) {
        if (uni::isLetter(c)) { token.push_back(c); continue; }
        if (check()) return true;
        token.clear();
    }
    return check();
}

}  // namespace

std::string apply(const std::string& sourceUtf8, const std::string& translationUtf8) {
    const auto translation = uni::decode(translationUtf8);
    if (!uni::anyLetter(translation)) return translationUtf8;
    const auto source = uni::decode(sourceUtf8);
    auto text = quotes32(source, translation);
    text = removeSpaceBeforePunctuation(text);
    text = uni::trim(collapseSpaces(text));
    return uni::encode(ending32(source, text));
}

std::string ending(const std::string& source, const std::string& translation) {
    return uni::encode(ending32(uni::decode(source), uni::decode(translation)));
}

std::string quotes(const std::string& source, const std::string& translation) {
    return uni::encode(quotes32(uni::decode(source), uni::decode(translation)));
}

char32_t apostrophe(const std::string& source) { return uni::contains(uni::decode(source), U'’') ? U'’' : U'\''; }

std::vector<std::optional<std::string>> registerForm(const std::vector<std::optional<std::string>>& translations) {
    const bool sen = std::any_of(translations.begin(), translations.end(), [](const auto& t) {
        return t && containsWord(uni::decode(*t), std::begin(kSenWords), std::end(kSenWords));
    });
    if (!sen) return translations;
    std::vector<std::optional<std::string>> out;
    for (const auto& t : translations) {
        if (!t) { out.push_back(std::nullopt); continue; }
        const auto text = uni::decode(*t);
        out.push_back(containsWord(text, std::begin(kSizWords), std::end(kSizWords)) ? *t : uni::encode(informal(text)));
    }
    return out;
}

}  // namespace crispplay::subs::TurkishPostEdit
