// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/FreeTranslation.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include "crispplay/subs/Glossary.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace crispplay::subs {

// Text pipeline of the offline translation: casing -> game phrases -> term protection ->
// engine -> terms and suffixes -> Turkish fixes. A short line (3 words or fewer) is translated
// together with the previous sentence; the part after the separator is taken, and if the split
// is ambiguous the line is translated alone.
namespace FreeTranslation {

// Version of the rules before/after translation; part of the cache's engine name.
constexpr int PipelineVersion = 2;
// Sentences with fewer words are translated with the previous sentence.
constexpr int ShortLineWords = 4;
// Context/line separator: "▁***" is one piece in the OPUS-MT vocabulary.
inline constexpr const char* Separator = " *** ";

using Engine = std::function<std::optional<std::string>(const std::string&)>;  // nullopt = failure
using Normalizer = std::function<std::string(const std::string&)>;

bool isShort(const std::string& sentence);
std::string cacheKey(const std::string& sentence, const std::optional<std::string>& context, const Glossary& glossary);
// The part after the separator; nullopt if the separator is not there exactly once, either side
// has no letters, or the part is implausibly long.
std::optional<std::string> afterSeparator(const std::string& output, const std::string& source);
// One sentence. With a context first, then alone, then (if terms could not be restored)
// unprotected. nullopt if an engine call failed.
std::optional<std::string> translate(const std::string& sentence, const std::optional<std::string>& context,
                                     const Glossary& glossary, const Normalizer& normalize, const Engine& engine);

}  // namespace FreeTranslation

// The last shown line: context of a short line. A reread of the same line gets the same
// context; a context older than MaxAgeMs is none.
class RecentLines {
public:
    static constexpr int64_t MaxAgeMs = 20000;
    using Clock = std::function<int64_t()>;  // milliseconds
    explicit RecentLines(Clock clock = {});
    // Records the line as shown; returns the line shown before it (if any).
    std::optional<std::string> shown(const std::string& text);

private:
    struct Last { std::string text; std::optional<std::string> context; int64_t seen; };
    Clock clock_;
    std::mutex mutex_;
    std::optional<Last> last_;
};

}  // namespace crispplay::subs
