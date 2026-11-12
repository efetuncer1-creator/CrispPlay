// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Translation/SubtitleSegmenter.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace crispplay::subs {

// Feeds the model one sentence at a time (whole subtitles made it drop short sentences and
// second rows). Speaker labels ("ATREUS:", "Kratos:") and dialogue dashes are kept as they
// are, because the model deletes or translates them.
namespace SubtitleSegmenter {

// A piece to translate, or a separator/label kept as is.
struct Piece {
    std::string text;
    bool translate = false;
    bool operator==(const Piece&) const = default;
};

std::vector<Piece> split(const std::string& text);
// The name in a label piece ("ATREUS: " -> "ATREUS"); nullopt if not a label.
std::optional<std::string> labelName(const Piece& piece);
// Speaker label at the start of a line, without the colon ("MINE GUARD: Hi" -> "MINE GUARD").
std::optional<std::string> speakerLabel(const std::string& line);
// An all-caps sentence in sentence case (the model leaves capitalised words untranslated);
// known names (lower case) stay capitalised. A single word is left alone.
std::string normalizeCase(const std::string& sentence, const std::set<std::string>* names = nullptr);

}  // namespace SubtitleSegmenter
}  // namespace crispplay::subs
