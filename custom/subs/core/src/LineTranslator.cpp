// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of the text flow of CouchSubs
// windows/src/CouchSubs.Translation/MarianTranslator.cs (TranslateAsync, TranslateSentenceAsync).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/LineTranslator.h"

#include "crispplay/subs/SubtitleSegmenter.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/TurkishPostEdit.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <vector>

namespace crispplay::subs {
namespace {

constexpr char32_t kUnknown = 0x2047;  // ⁇, SentencePiece's rendering of <unk>

bool wordy(char32_t c) { return uni::isLetter(c) || uni::isMark(c); }

std::u32string collapse(const std::u32string& s) { return uni::join(uni::splitWhiteSpace(s), U" "); }

bool startsWithIgnoreCase(std::u32string_view text, std::u32string_view prefix) {
    return prefix.size() <= text.size() && uni::equalsIgnoreCase(text.substr(0, prefix.size()), prefix);
}
bool endsWithIgnoreCase(std::u32string_view text, std::u32string_view suffix) {
    return suffix.size() <= text.size() && uni::equalsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

std::vector<std::u32string> inputWords(const std::u32string& input) {
    std::vector<std::u32string> words;
    std::u32string current;
    for (char32_t c : input) {
        if (wordy(c)) { current.push_back(c); continue; }
        if (!current.empty() && std::find(words.begin(), words.end(), current) == words.end()) words.push_back(current);
        current.clear();
    }
    if (!current.empty() && std::find(words.begin(), words.end(), current) == words.end()) words.push_back(current);
    return words;
}

std::optional<std::u32string> uniqueWord(const std::vector<std::u32string>& words, const std::u32string& prefix,
                                         const std::u32string& suffix) {
    if (prefix.empty() && suffix.empty()) return std::nullopt;
    std::optional<std::u32string> found;
    for (const auto& w : words) {
        if (w.size() <= prefix.size() + suffix.size() || !startsWithIgnoreCase(w, prefix) || !endsWithIgnoreCase(w, suffix)) continue;
        if (found) return std::nullopt;  // ambiguous
        found = w;
    }
    return found;
}

}  // namespace

UnknownMarkResult handleUnknownMarks(const std::string& outputUtf8, const std::string& inputUtf8) {
    auto s = uni::decode(outputUtf8);
    if (!uni::contains(s, kUnknown)) return {outputUtf8, false};
    const auto input = uni::decode(inputUtf8);
    auto stripped = s;
    std::replace(stripped.begin(), stripped.end(), kUnknown, U' ');
    stripped = collapse(stripped);
    // Nothing usable left, or only the source spelled without its unknown letters: keep the source.
    if ((!uni::anyLetter(stripped) && uni::anyLetter(input)) ||
        TextMatch::isSimilar(TextMatch::normalize(uni::encode(stripped)), TextMatch::normalize(inputUtf8), 0.7))
        return {inputUtf8, true};

    const auto words = inputWords(input);
    size_t pos = 0;
    while (true) {
        const size_t p = s.find(kUnknown, pos);
        if (p == std::u32string::npos) break;
        size_t le = p;
        if (le > 0 && s[le - 1] == U' ') --le;
        size_t ls = le;
        while (ls > 0 && wordy(s[ls - 1])) --ls;
        size_t rs = p + 1;
        if (rs < s.size() && s[rs] == U' ') ++rs;
        size_t re = rs;
        while (re < s.size() && wordy(s[re])) ++re;
        const auto left = s.substr(ls, le - ls), right = s.substr(rs, re - rs);
        bool replaced = false;
        const std::pair<bool, bool> combos[] = {{true, true}, {true, false}, {false, true}};
        for (auto [useLeft, useRight] : combos) {
            const auto prefix = useLeft ? left : std::u32string();
            const auto suffix = useRight ? right : std::u32string();
            if ((useLeft && left.empty()) || (useRight && right.empty())) continue;
            if (auto word = uniqueWord(words, prefix, suffix)) {
                const size_t start = useLeft ? ls : p, end = useRight ? re : p + 1;
                s.replace(start, end - start, *word);
                pos = start + word->size();
                replaced = true;
                break;
            }
        }
        if (replaced) continue;
        s.erase(p, 1);
        pos = p;
    }
    return {uni::encode(collapse(s)), false};
}

LineTranslator::LineTranslator(Translator& engine, Options options)
    : engine_(engine), options_(std::move(options)), recent_(options_.clock) {
    cache_ = options_.cache;
    if (!cache_) {
        cache_ = options_.cacheDirectory ? TranslationCache::shared(*options_.cacheDirectory, options_.engineId, "en", "tr")
                                         : std::make_shared<TranslationCache>();
    }
    glossary_ = options_.glossary ? options_.glossary : std::make_shared<GlossaryStore>(options_.glossaryDirectory);
}

std::optional<TranslatedLine> LineTranslator::translate(const std::string& text) {
    std::lock_guard lock(mutex_);
    if (!options_.textPipeline) return translateDirect(text);

    // Context of a short sentence: the previous sentence of the line, else the last sentence of
    // the line shown before this one.
    const auto previousLine = recent_.shown(text);
    const auto pieces = SubtitleSegmenter::split(text);
    // "OLD MAN: " -> "old", "man": each word of a name is known.
    for (const auto& piece : pieces)
        if (auto name = SubtitleSegmenter::labelName(piece))
            for (const auto& word : uni::splitWhiteSpace(uni::decode(*name)))
                if (word.size() >= 2) names_.insert(uni::encode(uni::toLowerInvariant(word)));
    std::optional<std::string> previous;
    if (previousLine)
        for (const auto& piece : SubtitleSegmenter::split(*previousLine))
            if (piece.translate) previous = piece.text;

    std::vector<std::optional<std::string>> translations;
    size_t translatable = 0, originals = 0;
    for (const auto& piece : pieces) {
        if (!piece.translate) {
            translations.push_back(std::nullopt);
            continue;
        }
        const auto context = FreeTranslation::isShort(piece.text) ? previous : std::nullopt;
        auto sentence = translateSentence(piece.text, context);
        if (!sentence) return std::nullopt;
        ++translatable;
        if (sentence->original) ++originals;
        translations.push_back(sentence->text);
        previous = piece.text;
    }
    // The register looks at the whole line; cached sentence translations stay as they are.
    const auto final = TurkishPostEdit::registerForm(translations);
    TranslatedLine out;
    size_t first = 0;
    if (!pieces.empty())
        if (auto name = SubtitleSegmenter::labelName(pieces[0])) {
            out.speaker = *name;
            out.full = pieces[0].text;
            first = 1;
        }
    for (size_t i = first; i < pieces.size(); ++i) out.text += final[i] ? *final[i] : pieces[i].text;
    out.full += out.text;
    out.original = translatable > 0 && originals == translatable;
    return out;
}

std::optional<LineTranslator::Sentence> LineTranslator::translateSentence(const std::string& sentence,
                                                                           const std::optional<std::string>& context) {
    const auto glossary = glossary_->current();
    const auto key = FreeTranslation::cacheKey(sentence, context, *glossary);
    if (auto known = cache_->get(key)) return Sentence{*known, *known == sentence};
    bool keptOriginal = false;
    std::set<std::string> names = names_;
    names.insert(glossary->capitalWords().begin(), glossary->capitalWords().end());
    auto result = FreeTranslation::translate(
        sentence, context, *glossary,
        [&](const std::string& t) { return SubtitleSegmenter::normalizeCase(t, &names); },
        [&](const std::string& input) -> std::optional<std::string> {
            auto output = engine_.translate(input);
            if (!output) return std::nullopt;
            auto handled = handleUnknownMarks(*output, input);
            keptOriginal = keptOriginal || handled.keptOriginal;
            return handled.text;
        });
    if (!result) return std::nullopt;
    if (keptOriginal) result = sentence;
    cache_->store(key, *result);
    return Sentence{*result, *result == sentence};
}

std::optional<TranslatedLine> LineTranslator::translateDirect(const std::string& text) {
    TranslatedLine out;
    std::string body = text;
    if (auto name = SubtitleSegmenter::speakerLabel(text)) {
        const auto trimmed = uni::trim(uni::decode(text));
        out.speaker = *name;
        body = uni::encode(uni::trim(trimmed.substr(uni::decode(*name).size() + 1)));
    }
    auto output = engine_.translate(body);
    if (!output) return std::nullopt;
    auto handled = handleUnknownMarks(*output, body);
    out.text = handled.text;
    out.original = handled.keptOriginal || handled.text == body;
    out.full = out.speaker.empty() ? out.text : out.speaker + ": " + out.text;
    return out;
}

}  // namespace crispplay::subs
