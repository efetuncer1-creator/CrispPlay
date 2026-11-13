// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of the text rules of CouchSubs game packs:
// windows/src/CouchSubs.Core/GamePacks.cs (PackKey), PackFuzzy.cs (signature, similarity) and
// windows/src/CouchSubs.Translation/PackLookup.cs (LooseLabel). Pack files and lookup are not
// ported yet (Phase 5 plan: later, once the content licence is clear).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace crispplay::subs {

namespace PackKey {
// 1. <tags> -> space, 2. NFKC, 3. lower case, 4. NFD and marks dropped, 5. everything but letters
// and digits -> space, 6. single spaces, trimmed. Unicode normalization uses the Windows API
// (NormalizeString); on other systems steps 2 and 4 are skipped.
std::string canon(const std::string& text);
// First 8 bytes of SHA-256(canon) as 16 lower-case hex digits; nullopt without letters/digits.
std::optional<std::string> key(const std::string& text);
std::string stripTags(const std::string& text, const std::string& replacement = " ");
// SHA-256 of bytes as lower-case hex (exposed for tests).
std::string sha256Hex(const std::string& bytes);
}  // namespace PackKey

namespace PackFuzzy {
constexpr int MinLength = 16;
constexpr int MaxBits = 256;
constexpr double Margin = 5;

// canon text without spaces, OCR look-alikes merged (l/1/ı/j -> i, 0 -> o, 5 -> s ...).
std::u32string folded(const std::string& text);
int bits(int length);
// OCR trace: non-ASCII letters, a non-letter inside a word, letter/digit mixes, odd one-letter
// words, glued long words.
bool damaged(const std::string& text);
std::vector<uint64_t> bloom(const std::u32string& folded, int bits);
// (length, Bloom bytes as lower-case hex); nullopt for a line shorter than MinLength.
std::optional<std::pair<int, std::string>> signature(const std::string& text);
double score(int both, int inQuery, int inEntry, int bits);
double similarity(const std::vector<uint64_t>& query, const std::vector<uint64_t>& entry, int bits);
// Minimum similarity (0-100) by OCR trace and length.
double required(int length, bool damaged);
}  // namespace PackFuzzy

// A speaker label damaged by OCR ("-GAETANO:", "M]:", "MİNE GUARD•", "SEBASTIAN Where...") as
// (label kept on screen, body); nullopt if none.
std::optional<std::pair<std::string, std::string>> looseLabel(const std::string& line);

}  // namespace crispplay::subs
