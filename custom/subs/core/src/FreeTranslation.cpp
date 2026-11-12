// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/FreeTranslation.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port change: the engine reports failure with nullopt instead of an exception.
#include "crispplay/subs/FreeTranslation.h"

#include "crispplay/subs/GameEnglish.h"
#include "crispplay/subs/TermProtection.h"
#include "crispplay/subs/TurkishPostEdit.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <chrono>

namespace crispplay::subs {
namespace FreeTranslation {
namespace {
constexpr char kKeyJoin = '\x1F';
}

bool isShort(const std::string& sentence) {
    const auto words = uni::splitWhiteSpace(uni::decode(sentence));
    const auto count = std::count_if(words.begin(), words.end(), [](const auto& w) { return uni::anyLetter(w); });
    return count > 0 && count < ShortLineWords;
}

std::string cacheKey(const std::string& sentence, const std::optional<std::string>& context, const Glossary& glossary) {
    const std::string basis = context ? *context + kKeyJoin + sentence : sentence;
    return glossary.matches(basis) ? glossary.fingerprint() + kKeyJoin + basis : basis;
}

std::optional<std::string> afterSeparator(const std::string& outputUtf8, const std::string& source) {
    const auto output = uni::decode(outputUtf8);
    const std::u32string core = U"***";
    const size_t first = output.find(core);
    if (first == std::u32string::npos || output.find(core, first + core.size()) != std::u32string::npos) return std::nullopt;
    const auto before = output.substr(0, first);
    const auto after = uni::trim(output.substr(first + core.size()));
    // Glued stars ("****") are ambiguous too.
    if ((!after.empty() && after[0] == U'*') || (!before.empty() && before.back() == U'*')) return std::nullopt;
    if (!uni::anyLetter(before) || !uni::anyLetter(after)) return std::nullopt;
    if (after.size() > 3 * uni::decode(source).size() + 20) return std::nullopt;
    return uni::encode(after);
}

std::optional<std::string> translate(const std::string& sentence, const std::optional<std::string>& context,
                                     const Glossary& glossary, const Normalizer& normalize, const Engine& engine) {
    const char32_t apostrophe = TurkishPostEdit::apostrophe(sentence);
    auto prepare = [&](const std::string& text) { return GameEnglish::plain(normalize(text)); };

    if (context) {
        TermProtector protector(glossary, {sentence, *context});
        const auto contextText = protector.protect(prepare(*context));
        const auto sentenceText = protector.protect(prepare(sentence));
        const auto output = engine(contextText.text + Separator + sentenceText.text);
        if (!output) return std::nullopt;
        if (auto after = afterSeparator(*output, sentence))
            if (auto restored = TermProtector::restore(*after, sentenceText.slots, protector.allSlots(), apostrophe))
                return TurkishPostEdit::apply(sentence, *restored);
    }

    TermProtector single(glossary, {sentence});
    const auto input = single.protect(prepare(sentence));
    const auto translated = engine(input.text);
    if (!translated) return std::nullopt;
    if (input.slots.empty()) return TurkishPostEdit::apply(sentence, *translated);
    if (auto done = TermProtector::restore(*translated, input.slots, single.allSlots(), apostrophe))
        return TurkishPostEdit::apply(sentence, *done);
    // The model dropped a placeholder: a complete translation without terms beats one missing a term.
    const auto plain = engine(prepare(sentence));
    if (!plain) return std::nullopt;
    return TurkishPostEdit::apply(sentence, *plain);
}

}  // namespace FreeTranslation

namespace {
int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

RecentLines::RecentLines(Clock clock) : clock_(clock ? std::move(clock) : Clock(steadyMs)) {}

std::optional<std::string> RecentLines::shown(const std::string& text) {
    std::lock_guard lock(mutex_);
    const int64_t now = clock_();
    if (last_ && last_->text == text) {
        last_->seen = now;
        return last_->context;
    }
    std::optional<std::string> context;
    if (last_ && now - last_->seen <= MaxAgeMs) context = last_->text;
    last_ = Last{text, context, now};
    return context;
}

}  // namespace crispplay::subs
