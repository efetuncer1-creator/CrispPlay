// CrispPlay subtitle core: UTF-8 / UTF-32 helpers and the .NET character predicates the
// CouchSubs port relies on (char.IsLetter, IsUpper, ToLowerInvariant, ...). Std-only; the
// tables come from tools/gen_unicode_tables.py. Part of CrispPlay, GPL-3.0 (see LICENSE).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crispplay::subs::uni {

// Unicode general categories, same order as .NET's UnicodeCategory where it matters.
enum class Cat : uint8_t {
    Lu, Ll, Lt, Lm, Lo, Mn, Mc, Me, Nd, Nl, No, Pc, Pd, Ps, Pe,
    Pi, Pf, Po, Sm, Sc, Sk, So, Zs, Zl, Zp, Cc, Cf, Cs, Co, Cn,
};

// Invalid UTF-8 bytes decode to U+FFFD; nothing throws.
std::u32string decode(std::string_view utf8);
std::string encode(std::u32string_view text);
std::string encode(char32_t c);

Cat category(char32_t c);
bool isLetter(char32_t c);          // Lu Ll Lt Lm Lo
bool isUpper(char32_t c);           // Lu
bool isLower(char32_t c);           // Ll
bool isDigit(char32_t c);           // Nd
bool isNumber(char32_t c);          // Nd Nl No
bool isLetterOrDigit(char32_t c);   // letter or Nd
bool isMark(char32_t c);            // Mn Mc Me
bool isPunctuation(char32_t c);     // Pc Pd Ps Pe Pi Pf Po
bool isSymbol(char32_t c);          // Sm Sc Sk So
bool isWhiteSpace(char32_t c);      // .NET char.IsWhiteSpace
bool isWordChar(char32_t c);        // .NET regex \w: L, Mn, Nd, Pc

// .NET invariant casing: simple one-to-one mapping, except that U+0130 (İ) is not
// lowered and U+0131 (ı) is not uppered (ICU-based .NET does the same).
char32_t toLowerInvariant(char32_t c);
char32_t toUpperInvariant(char32_t c);
// tr-TR casing: I <-> ı, İ <-> i, otherwise the simple mapping.
char32_t toLowerTurkish(char32_t c);
char32_t toUpperTurkish(char32_t c);

std::u32string toLowerInvariant(std::u32string_view text);
std::u32string toUpperInvariant(std::u32string_view text);
std::u32string toLowerTurkish(std::u32string_view text);

// Case-insensitive (invariant) comparison of single characters and strings.
bool equalsIgnoreCase(char32_t a, char32_t b);
bool equalsIgnoreCase(std::u32string_view a, std::u32string_view b);
// Ordinal-ignore-case search like String.IndexOf(..., OrdinalIgnoreCase); npos if absent.
size_t findIgnoreCase(std::u32string_view text, std::u32string_view part, size_t from = 0);

// string.Split((char[])null, RemoveEmptyEntries): split on .NET white space.
std::vector<std::u32string> splitWhiteSpace(std::u32string_view text);
// string.Split(c) keeping empty parts.
std::vector<std::u32string> split(std::u32string_view text, char32_t separator);
std::u32string join(const std::vector<std::u32string>& parts, std::u32string_view separator);
std::u32string trim(std::u32string_view text);       // .NET Trim(): white space
std::u32string trimEnd(std::u32string_view text);
bool contains(std::u32string_view text, char32_t c);
bool contains(std::u32string_view text, std::u32string_view part);
bool startsWith(std::u32string_view text, std::u32string_view prefix);
bool endsWith(std::u32string_view text, std::u32string_view suffix);
std::u32string replaceAll(std::u32string_view text, std::u32string_view from, std::u32string_view to);
bool anyLetter(std::u32string_view text);

}  // namespace crispplay::subs::uni
