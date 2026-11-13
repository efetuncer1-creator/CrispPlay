#pragma once
// Text-level helpers used to pick between two readings of the same line.
// Ported from the Phase 5 spike (custom/subs/spike/ocr_bench.py `wordiness`,
// real_report.py `fix`). Strings are UTF-8.
#include <string>
#include <string_view>

namespace crispplay::subs::ocr {

// How word-like a reading is: letters in clean words (plain/Title/UPPER case,
// optional ' or - joins, surrounding quotes/punctuation allowed, or a number)
// minus twice the length of every other token.
int wordiness(std::string_view text);

// The spike's three small repairs (same idea as CouchSubs' OcrNoise, written
// from its description): a lone "l" is "I", leading non-letter junk goes, and a
// reading that is the same words twice becomes one copy. Also collapses spaces.
// The text pipeline (custom/subs/core) may apply its fuller OcrNoise port later.
std::string repairReading(std::string_view text);

// Whitespace collapsed to single spaces, trimmed.
std::string collapseSpaces(std::string_view text);

// True when the text holds an ASCII letter or digit.
bool hasAlnum(std::string_view text);

}  // namespace crispplay::subs::ocr
