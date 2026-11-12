// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/OcrNoise.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <string>

namespace crispplay::subs {

// OCR turns some words of fancy, fading or flickering subtitles into letter/digit/symbol
// garbage ("ou$3e", "waters2aljd"). Garbage is dropped, garbage glued to a real word is cut
// off, usual forms ("PS5", "R2", "2nd", "100m", "$500") stay.
namespace OcrNoise {

std::string clean(const std::string& line);
// How word-like a reading is: letters of clean words count plus, garbled ones minus twice.
int score(const std::string& text);
// More than a sixth of the letters are in broken words (mixed case, symbols, no vowels).
bool looksGarbled(const std::string& text);

}  // namespace OcrNoise
}  // namespace crispplay::subs
