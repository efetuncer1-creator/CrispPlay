// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of the text flow of CouchSubs
// windows/src/CouchSubs.Translation/MarianTranslator.cs (the ONNX engine itself is not ported;
// the engine is a crispplay::subs::Translator, e.g. CTranslate2).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// CrispPlay additions: speaker label returned separately (an OCR-damaged "NAME:"/"NAME•" label too,
// via PackLookup's LooseLabel rule), and the SentencePiece unknown mark
// U+2047 "⁇" handled (original line, restored foreign word, or the mark dropped).
#pragma once

#include "crispplay/subs/FreeTranslation.h"
#include "crispplay/subs/Glossary.h"
#include "crispplay/subs/TranslationCache.h"
#include "crispplay/subs/Types.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace crispplay::subs {

struct TranslatedLine {
    std::string speaker;  // leading speaker label without the colon ("MINE GUARD"), or empty
    std::string text;     // translation without that label
    bool original = false;  // no sentence could be translated usefully: `text` is the English
    std::string full;     // whole translated line, labels included (CouchSubs' result)
};

// What the engine said about one call (U+2047 handling).
struct UnknownMarkResult {
    std::string text;
    bool keptOriginal = false;  // output without "⁇" was the source itself (name, foreign word)
};
// The engine output for `input` with U+2047 "⁇" (SentencePiece unknown) handled:
// 1. if the output without the marks is similar to the input (a name or foreign word the model
//    could not spell), the input itself is returned and keptOriginal is set;
// 2. a word around a mark that matches exactly one input word with the same start and end
//    ("Str ⁇ nzu" / "Strùnzu") is replaced by that input word;
// 3. any remaining mark is dropped.
UnknownMarkResult handleUnknownMarks(const std::string& output, const std::string& input);

// Translates one subtitle line (possibly several rows) with a sentence engine.
class LineTranslator {
public:
    struct Options {
        // Folder for the persistent cache file; none = memory only.
        std::optional<std::filesystem::path> cacheDirectory;
        // User glossary folder (*.json) on top of the built-in glossary; none = built-in only.
        std::optional<std::filesystem::path> glossaryDirectory;
        // Cache file engine name; changes with the model or the text rules.
        std::string engineId = defaultEngineId();
        // false: no sentence splitting, glossary, context or post-edit; the engine gets the line
        // without its speaker label (whole-line engines, tests).
        bool textPipeline = true;
        std::function<int64_t()> clock;  // milliseconds, for the short-line context age
        std::shared_ptr<TranslationCache> cache;   // overrides cacheDirectory
        std::shared_ptr<GlossaryStore> glossary;   // overrides glossaryDirectory
    };

    static std::string defaultEngineId() { return "opus-mt-p" + std::to_string(FreeTranslation::PipelineVersion); }

    LineTranslator(Translator& engine, Options options);

    // Blocking; nullopt if the engine failed (or was cancelled).
    std::optional<TranslatedLine> translate(const std::string& line);
    void cancel() { engine_.cancel(); }

private:
    struct Sentence { std::string text; bool original = false; };
    std::optional<Sentence> translateSentence(const std::string& sentence, const std::optional<std::string>& context);
    std::optional<TranslatedLine> translateLocked(const std::string& line);
    std::optional<TranslatedLine> translateDirect(const std::string& line);

    Translator& engine_;
    Options options_;
    std::shared_ptr<TranslationCache> cache_;
    std::shared_ptr<GlossaryStore> glossary_;
    RecentLines recent_;
    std::mutex mutex_;  // one translation at a time
    std::set<std::string> names_;  // lower-case words of speaker labels
};

}  // namespace crispplay::subs
