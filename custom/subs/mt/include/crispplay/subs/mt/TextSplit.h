#pragma once

// Pure text helpers of the OPUS-MT translator (no CTranslate2 dependency, so the
// unit tests build without the model runtime).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace crispplay::subs::mt {

// Splits a subtitle into sentences the way custom/subs/spike/mt_bench.py does:
// a break follows a run of . ! ? or U+2026, optional closing quotes/brackets and
// whitespace, when the next word starts (after optional opening quotes or "(")
// with an ASCII capital or digit. "Dr. Kovacs" style abbreviations stay
// together. Pieces are trimmed; whitespace-only input gives an empty list.
std::vector<std::string> splitSentences(std::string_view text);

// Joins translated sentences with one space (the spike's " ".join).
std::string joinSentences(const std::vector<std::string>& parts);

// ASCII whitespace trim.
std::string_view trim(std::string_view text);

// Cuts UTF-8 text to at most maxBytes without splitting a code point, preferring
// the last space in the final quarter so a word is not cut in half.
std::string truncateUtf8(std::string_view text, std::size_t maxBytes);

// U+2047 DOUBLE QUESTION MARK: what SentencePiece writes for an <unk> piece.
inline constexpr std::string_view kUnknownMarker = "\xE2\x81\x87";

// True when a decoded translation contains the unknown-token marker.
bool containsUnknownMarker(std::string_view translated);

} // namespace crispplay::subs::mt
