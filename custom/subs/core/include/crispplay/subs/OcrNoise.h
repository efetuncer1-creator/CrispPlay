// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/OcrNoise.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <functional>
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

// CrispPlay additions (a partly read line over a bright background: "Take it, nd kill shose wh").
// A lower case token that cannot be an English word: one or two letters that are no English
// word ("nd", "wh", "ke"), three or more without a vowel ("rnnk"), or starting with two
// consonants no English word starts with ("ndkill"), or 17+ letters (words run together).
// Capitalised tokens are names and pass.
bool isBrokenWord(const std::string& token);
// Tokens of the text for which isBrokenWord holds, that are garbage in the looksGarbled sense,
// or that lost a space ("you.Take", "it,and").
int brokenWords(const std::string& text);

// English word list the rules may consult (the app gives Windows' spell checker; none = no
// dictionary rule). Called with one lower case word ("don't" keeps its apostrophe); must be
// callable from the threads that call the functions below. Process-wide.
using Dictionary = std::function<bool(const std::string& word)>;
void setDictionary(Dictionary knows);
// Lower case words of 3+ letters the dictionary does not know (0 without a dictionary).
int unknownWords(const std::string& text);

// A reading worth waiting for a better one: garbled, holding a broken word, or with two or
// more unknown words that make at least a fifth of its lower case words.
bool looksBroken(const std::string& text);
// score() with broken and unknown words counted as garbage: the better of two readings of one line.
int quality(const std::string& text);

}  // namespace OcrNoise
}  // namespace crispplay::subs
