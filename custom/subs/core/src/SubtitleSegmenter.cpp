// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Translation/SubtitleSegmenter.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: the .NET regular expressions (speaker label, sentence gap with a variable-length
// lookbehind, dialogue dash) are hand-written scanners with the same matches.
#include "crispplay/subs/SubtitleSegmenter.h"

#include "crispplay/subs/Unicode.h"

#include <algorithm>

namespace crispplay::subs::SubtitleSegmenter {
namespace {

using uni::contains;

// Capitalised headings ending in a colon that are not names; they are translated.
const std::u32string_view kNotNames[] = {
    U"note", U"warning", U"tip", U"hint", U"objective", U"objectives", U"quest", U"goal", U"new", U"reward", U"rewards",
    U"location", U"chapter", U"mission", U"task", U"tutorial", U"update", U"info", U"status", U"level", U"controls",
    U"press", U"hold", U"tap", U"remember", U"example", U"question", U"answer", U"result", U"results", U"time",
    U"listen", U"look", U"wait", U"okay", U"ok", U"well", U"so", U"yes", U"no", U"now", U"then", U"and", U"but"};

// Abbreviations after which a full stop does not end the sentence ("Mr. Smith").
const std::u32string_view kAbbreviations[] = {U"mr", U"mrs", U"ms", U"dr", U"st", U"jr", U"sr", U"prof", U"mt", U"vs",
                                              U"etc", U"e.g", U"i.e", U"lt", U"sgt", U"capt", U"gen", U"col"};

bool inList(const std::u32string& word, const std::u32string_view* begin, const std::u32string_view* end) {
    return std::any_of(begin, end, [&](std::u32string_view w) { return uni::equalsIgnoreCase(word, w); });
}

bool capsWordChar(char32_t c) { return uni::isUpper(c) || uni::isDigit(c) || contains(U"'’.-", c); }
bool mixedWordChar(char32_t c) { return uni::isLetter(c) || contains(U"'’.-", c); }

// Name part of ^(?<name>\p{Lu}[\p{Lu}\d'’.\-]+(?:[ ]\p{Lu}[\p{Lu}\d'’.\-]*){0,2}
//                    |\p{Lu}\p{Ll}[\p{L}'’.\-]*(?:[ ]\p{Lu}[\p{L}'’.\-]*){0,2}):
bool isName(const std::u32string& name) {
    const auto words = uni::split(name, U' ');
    if (words.empty() || words.size() > 3) return false;
    for (const auto& w : words)
        if (w.empty() || !uni::isUpper(w[0])) return false;
    auto all = [](const std::u32string& w, size_t from, bool (*pred)(char32_t)) {
        return std::all_of(w.begin() + from, w.end(), pred);
    };
    // All capitals ("ATREUS", "OLD MAN", "SINDRI JR.")
    bool caps = words[0].size() >= 2 && all(words[0], 1, capsWordChar);
    for (size_t i = 1; caps && i < words.size(); ++i) caps = all(words[i], 1, capsWordChar);
    if (caps) return true;
    // Capitalised words ("Kratos", "Mimir Head")
    bool mixed = words[0].size() >= 2 && uni::isLower(words[0][1]) && all(words[0], 2, mixedWordChar);
    for (size_t i = 1; mixed && i < words.size(); ++i) mixed = all(words[i], 1, mixedWordChar);
    return mixed;
}

struct Label {
    std::u32string label;  // name + ':'
    std::u32string name;
    size_t length = 0;     // label plus the white space after it
};

// Speaker label at the start; one-word headings ("Note:", "Objective:") are not names.
std::optional<Label> speakerLabel32(const std::u32string& text) {
    const size_t colon = text.find(U':');
    if (colon == std::u32string::npos || colon + 1 >= text.size() || !uni::isWhiteSpace(text[colon + 1])) return std::nullopt;
    std::u32string name = text.substr(0, colon);
    if (!isName(name)) return std::nullopt;
    if (!contains(name, U' ') && inList(name, std::begin(kNotNames), std::end(kNotNames))) return std::nullopt;
    size_t end = colon + 1;
    while (end < text.size() && uni::isWhiteSpace(text[end])) ++end;
    return Label{name + U":", name, end};
}

// ^[-–—]+\s*  -> (dash, length)
std::optional<std::pair<std::u32string, size_t>> dialogueDash(const std::u32string& text) {
    size_t i = 0;
    while (i < text.size() && contains(U"-–—", text[i])) ++i;
    if (i == 0) return std::nullopt;
    const std::u32string dash = text.substr(0, i);
    while (i < text.size() && uni::isWhiteSpace(text[i])) ++i;
    return std::make_pair(dash, i);
}

std::u32string collapseWhiteSpace(const std::u32string& text) {
    std::u32string out;
    bool gap = false;
    for (char32_t c : text) {
        if (uni::isWhiteSpace(c)) { gap = true; continue; }
        if (gap) out.push_back(U' ');
        gap = false;
        out.push_back(c);
    }
    if (gap) out.push_back(U' ');
    return out;
}

bool endsWithAbbreviation(const std::u32string& sentence) {
    if (sentence.empty() || sentence.back() != U'.') return false;
    const size_t lastSpace = sentence.rfind(U' ');
    const size_t start = lastSpace == std::u32string::npos ? 0 : lastSpace + 1;
    const std::u32string word = sentence.substr(start, sentence.size() - 1 - start);
    // A single capital is an initial ("J. R. R.").
    return (word.size() == 1 && uni::isUpper(word[0])) || inList(word, std::begin(kAbbreviations), std::end(kAbbreviations));
}

// Gaps matching (?<=[.!?…]+["'”’)\]]*)\s+ : maximal white space runs after closers after a mark.
std::vector<std::u32string> splitSentences(const std::u32string& block) {
    std::vector<std::u32string> out;
    size_t start = 0, i = 0;
    while (i < block.size()) {
        if (!uni::isWhiteSpace(block[i])) { ++i; continue; }
        size_t end = i;
        while (end < block.size() && uni::isWhiteSpace(block[end])) ++end;
        size_t k = i;
        while (k > 0 && contains(U"\"'”’)]", block[k - 1])) --k;
        const bool gap = k > 0 && contains(U".!?…", block[k - 1]);
        if (gap) {
            const std::u32string candidate = block.substr(start, i - start);
            const char32_t next = end < block.size() ? block[end] : 0;
            // "wait... what": a lower case continuation and abbreviations do not end a sentence.
            if (!(uni::isLower(next) || endsWithAbbreviation(candidate))) {
                out.push_back(candidate);
                start = end;
            }
        }
        i = end;
    }
    if (start < block.size()) out.push_back(block.substr(start));
    return out;
}

void addBlock(std::vector<Piece>& pieces, std::u32string block) {
    block = collapseWhiteSpace(block);
    if (auto dash = dialogueDash(block)) {
        pieces.push_back({uni::encode(dash->first + U" "), false});
        block = block.substr(dash->second);
    }
    bool first = true;
    for (auto body : splitSentences(block)) {
        if (!first) pieces.push_back({" ", false});
        first = false;
        if (auto label = speakerLabel32(body)) {
            pieces.push_back({uni::encode(label->label + U" "), false});
            body = body.substr(label->length);
        }
        if (!body.empty()) pieces.push_back({uni::encode(body), uni::anyLetter(body)});
    }
}

}  // namespace

std::vector<Piece> split(const std::string& text) {
    std::vector<Piece> pieces;
    const auto lines = uni::split(uni::replaceAll(uni::decode(text), U"\r\n", U"\n"), U'\n');
    std::u32string block;
    std::string pendingSeparator;
    auto flush = [&] {
        if (block.empty()) return;
        if (!pendingSeparator.empty()) pieces.push_back({pendingSeparator, false});
        addBlock(pieces, block);
        block.clear();
        pendingSeparator = "\n";
    };
    for (const auto& raw : lines) {
        const auto line = uni::trim(raw);
        if (line.empty()) continue;
        // A labelled or dashed line is a new turn; other lines are wraps and get joined.
        if (speakerLabel32(line) || dialogueDash(line)) flush();
        if (!block.empty()) block.push_back(U' ');
        block += line;
    }
    flush();
    return pieces;
}

std::optional<std::string> labelName(const Piece& piece) {
    if (piece.translate || piece.text.size() < 2 || piece.text.compare(piece.text.size() - 2, 2, ": ") != 0) return std::nullopt;
    return piece.text.substr(0, piece.text.size() - 2);
}

std::optional<std::string> speakerLabel(const std::string& line) {
    if (auto label = speakerLabel32(uni::trim(uni::decode(line)))) return uni::encode(label->name);
    return std::nullopt;
}

std::string normalizeCase(const std::string& sentenceUtf8, const std::set<std::string>* names) {
    const auto sentence = uni::decode(sentenceUtf8);
    int letters = 0;
    for (char32_t c : sentence) {
        if (uni::isLower(c)) return sentenceUtf8;
        if (uni::isLetter(c)) ++letters;
    }
    if (letters < 2 || !contains(uni::trim(sentence), U' ')) return sentenceUtf8;

    std::u32string output, word;
    bool startOfSentence = true;
    auto flushWord = [&] {
        if (word.empty()) return;
        const auto lower = uni::toLowerInvariant(word);
        // "I" is always a capital in English.
        const bool capital = startOfSentence || lower == U"i" || (names && names->count(uni::encode(lower)) > 0);
        if (capital) {
            output.push_back(uni::toUpperInvariant(lower[0]));
            output += lower.substr(1);
        } else {
            output += lower;
        }
        startOfSentence = false;
        word.clear();
    };
    for (char32_t c : sentence) {
        if (uni::isLetter(c) || c == U'\'' || c == U'’') {
            word.push_back(c);
            continue;
        }
        flushWord();
        if (contains(U".!?…", c)) startOfSentence = true;
        output.push_back(c);
    }
    flushWord();
    return uni::encode(output);
}

}  // namespace crispplay::subs::SubtitleSegmenter
