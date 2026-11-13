// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TextMatch.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <string>

namespace crispplay::subs {

// Simplifying and noise tolerant comparison of OCR readings. All text is UTF-8.
namespace TextMatch {

// Lower case (invariant), letters/digits only, single spaces.
std::string normalize(const std::string& s);
// Levenshtein ratio >= threshold. Empty text is similar to nothing.
bool isSimilar(const std::string& a, const std::string& b, double threshold = 0.85);
// Rows normalized one by one and joined with '\n'.
std::string normalizeRows(const std::string& s);
// Is `reread` (normalized) a reread of the sent subtitle (normalizeRows of it)?
bool isSameSubtitle(const std::string& reread, const std::string& sent);
// Is the new line a grown or corrected reading of the shown one (raw texts)?
bool isRevision(const std::string& newText, const std::string& oldText);
// Is the reading a garbled, not better reread of the sent line (raw texts)?
bool isWorseReread(const std::string& reread, const std::string& sent);
// `newText` is `oldText` with words appended (or prepended, two words or more).
bool isGrowth(const std::string& newText, const std::string& oldText);

}  // namespace TextMatch
}  // namespace crispplay::subs
