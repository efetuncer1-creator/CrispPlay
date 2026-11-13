// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TurkishSuffix.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <string>

namespace crispplay::subs::TurkishSuffix {

// term + the Turkish case suffix the engine put on the placeholder ("a", "un", "ya", "dan"),
// rebuilt for the real term: vowel harmony, buffer letters, consonant assimilation,
// apostrophe for proper names, softening for common nouns, compound buffer "n".
std::string attach(const std::string& term, const std::string& suffix, char32_t apostrophe = U'\'');
std::u32string attach(const std::u32string& term, const std::u32string& suffix, char32_t apostrophe = U'\'');

}  // namespace crispplay::subs::TurkishSuffix
