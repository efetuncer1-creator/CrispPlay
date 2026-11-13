// CrispPlay subtitle core: Unicode helpers. Part of CrispPlay, GPL-3.0 (see LICENSE).
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <iterator>

namespace crispplay::subs::uni {
namespace {

struct CategoryRun { char32_t start; uint8_t cat; };
struct CaseRun { char32_t start; char32_t end; int stride; int delta; };

#include "UnicodeTables.inc"

template <size_t N>
char32_t mapCase(const CaseRun (&runs)[N], char32_t c) {
    auto it = std::upper_bound(std::begin(runs), std::end(runs), c,
                               [](char32_t value, const CaseRun& run) { return value < run.start; });
    if (it == std::begin(runs)) return c;
    const CaseRun& run = *std::prev(it);
    if (c > run.end || (c - run.start) % run.stride != 0) return c;
    return static_cast<char32_t>(static_cast<int64_t>(c) + run.delta);
}

void append(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

}  // namespace

std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        int extra = 0;
        char32_t c = 0;
        if (b < 0x80) { out.push_back(b); ++i; continue; }
        if ((b & 0xE0) == 0xC0) { extra = 1; c = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; c = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; c = b & 0x07; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + extra >= s.size()) { out.push_back(0xFFFD); ++i; continue; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto n = static_cast<unsigned char>(s[i + k]);
            if ((n & 0xC0) != 0x80) { ok = false; break; }
            c = (c << 6) | (n & 0x3F);
        }
        const bool overlong = (extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) || (extra == 3 && c < 0x10000);
        if (!ok || overlong || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(c);
        i += extra + 1;
    }
    return out;
}

std::string encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) append(out, c > 0x10FFFF ? 0xFFFD : c);
    return out;
}

std::string encode(char32_t c) { return encode(std::u32string_view(&c, 1)); }

Cat category(char32_t c) {
    if (c > 0x10FFFF) return Cat::Cn;
    auto it = std::upper_bound(std::begin(kCategoryRuns), std::end(kCategoryRuns), c,
                               [](char32_t value, const CategoryRun& run) { return value < run.start; });
    return static_cast<Cat>(std::prev(it)->cat);
}

bool isLetter(char32_t c) {
    if (c < 0x80) return (c | 0x20) >= 'a' && (c | 0x20) <= 'z';
    const Cat k = category(c);
    return k == Cat::Lu || k == Cat::Ll || k == Cat::Lt || k == Cat::Lm || k == Cat::Lo;
}
bool isUpper(char32_t c) { return c < 0x80 ? (c >= 'A' && c <= 'Z') : category(c) == Cat::Lu; }
bool isLower(char32_t c) { return c < 0x80 ? (c >= 'a' && c <= 'z') : category(c) == Cat::Ll; }
bool isDigit(char32_t c) { return c < 0x80 ? (c >= '0' && c <= '9') : category(c) == Cat::Nd; }
bool isNumber(char32_t c) {
    if (c < 0x80) return c >= '0' && c <= '9';
    const Cat k = category(c);
    return k == Cat::Nd || k == Cat::Nl || k == Cat::No;
}
bool isLetterOrDigit(char32_t c) { return isLetter(c) || isDigit(c); }
bool isMark(char32_t c) {
    const Cat k = category(c);
    return k == Cat::Mn || k == Cat::Mc || k == Cat::Me;
}
bool isPunctuation(char32_t c) {
    const Cat k = category(c);
    return k == Cat::Pc || k == Cat::Pd || k == Cat::Ps || k == Cat::Pe || k == Cat::Pi || k == Cat::Pf || k == Cat::Po;
}
bool isSymbol(char32_t c) {
    const Cat k = category(c);
    return k == Cat::Sm || k == Cat::Sc || k == Cat::Sk || k == Cat::So;
}
bool isWhiteSpace(char32_t c) {
    if (c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0) return true;
    if (c < 0x80) return false;
    const Cat k = category(c);
    return k == Cat::Zs || k == Cat::Zl || k == Cat::Zp;
}
bool isWordChar(char32_t c) {
    if (isLetter(c) || isDigit(c)) return true;
    const Cat k = category(c);
    return k == Cat::Mn || k == Cat::Pc;
}

char32_t toLowerInvariant(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c == 0x130) return c;
    return mapCase(kLowerRuns, c);
}
char32_t toUpperInvariant(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c == 0x131) return c;
    return mapCase(kUpperRuns, c);
}
char32_t toLowerTurkish(char32_t c) {
    if (c == 'I') return 0x131;
    if (c == 0x130) return 'i';
    return toLowerInvariant(c);
}
char32_t toUpperTurkish(char32_t c) {
    if (c == 'i') return 0x130;
    if (c == 0x131) return 'I';
    return toUpperInvariant(c);
}

std::u32string toLowerInvariant(std::u32string_view text) {
    std::u32string out(text);
    for (auto& c : out) c = toLowerInvariant(c);
    return out;
}
std::u32string toUpperInvariant(std::u32string_view text) {
    std::u32string out(text);
    for (auto& c : out) c = toUpperInvariant(c);
    return out;
}
std::u32string toLowerTurkish(std::u32string_view text) {
    std::u32string out(text);
    for (auto& c : out) c = toLowerTurkish(c);
    return out;
}

bool equalsIgnoreCase(char32_t a, char32_t b) { return a == b || toUpperInvariant(a) == toUpperInvariant(b); }

bool equalsIgnoreCase(std::u32string_view a, std::u32string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!equalsIgnoreCase(a[i], b[i])) return false;
    return true;
}

size_t findIgnoreCase(std::u32string_view text, std::u32string_view part, size_t from) {
    if (part.empty()) return from <= text.size() ? from : std::u32string::npos;
    if (part.size() > text.size()) return std::u32string::npos;
    for (size_t i = from; i + part.size() <= text.size(); ++i)
        if (equalsIgnoreCase(text.substr(i, part.size()), part)) return i;
    return std::u32string::npos;
}

std::vector<std::u32string> splitWhiteSpace(std::u32string_view text) {
    std::vector<std::u32string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isWhiteSpace(text[i])) ++i;
        size_t start = i;
        while (i < text.size() && !isWhiteSpace(text[i])) ++i;
        if (i > start) out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

std::vector<std::u32string> split(std::u32string_view text, char32_t separator) {
    std::vector<std::u32string> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == separator) {
            out.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::u32string join(const std::vector<std::u32string>& parts, std::u32string_view separator) {
    std::u32string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += separator;
        out += parts[i];
    }
    return out;
}

std::u32string trim(std::u32string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && isWhiteSpace(text[a])) ++a;
    while (b > a && isWhiteSpace(text[b - 1])) --b;
    return std::u32string(text.substr(a, b - a));
}

std::u32string trimEnd(std::u32string_view text) {
    size_t b = text.size();
    while (b > 0 && isWhiteSpace(text[b - 1])) --b;
    return std::u32string(text.substr(0, b));
}

bool contains(std::u32string_view text, char32_t c) { return text.find(c) != std::u32string_view::npos; }
bool contains(std::u32string_view text, std::u32string_view part) { return text.find(part) != std::u32string_view::npos; }
bool startsWith(std::u32string_view text, std::u32string_view prefix) { return text.substr(0, prefix.size()) == prefix; }
bool endsWith(std::u32string_view text, std::u32string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::u32string replaceAll(std::u32string_view text, std::u32string_view from, std::u32string_view to) {
    if (from.empty()) return std::u32string(text);
    std::u32string out;
    size_t pos = 0;
    while (true) {
        size_t hit = text.find(from, pos);
        if (hit == std::u32string_view::npos) break;
        out += text.substr(pos, hit - pos);
        out += to;
        pos = hit + from.size();
    }
    out += text.substr(pos);
    return out;
}

bool anyLetter(std::u32string_view text) {
    return std::any_of(text.begin(), text.end(), [](char32_t c) { return isLetter(c); });
}

}  // namespace crispplay::subs::uni
