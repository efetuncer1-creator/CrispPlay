#include "crispplay/subs/ocr/TextScore.h"

#include <algorithm>
#include <vector>

namespace crispplay::subs::ocr {
namespace {

std::u32string decodeUtf8(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto c = uint8_t(s[i]);
        char32_t cp;
        size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + n > s.size()) { out.push_back(0xFFFD); break; }
        for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (uint8_t(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

bool isSpace(char32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0xA0 || c == 0x3000 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x1680 ||
           c == 0x85;
}
bool isUpper(char32_t c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char32_t c) { return c >= 'a' && c <= 'z'; }
bool isLetter(char32_t c) { return isUpper(c) || isLower(c); }
bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isAsciiAlnum(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool isLead(char32_t c) { return c == '"' || c == '\'' || c == '(' || c == '[' || c == '-'; }
bool isTrail(char32_t c) {
    switch (c) {
        case '"': case '\'': case ')': case ']': case '.': case ',': case '!': case '?': case ':': case ';': case 0x2026:
        case '-':
            return true;
        default:
            return false;
    }
}
bool isJoin(char32_t c) { return c == '\'' || c == 0x2019 || c == '-'; }

std::vector<std::u32string> splitWords(const std::u32string& s) {
    std::vector<std::u32string> out;
    std::u32string cur;
    for (char32_t c : s) {
        if (isSpace(c)) {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// One alternative of PLAIN_CASE: [A-Z]?[a-z]+ | [A-Z]+
bool plainPart(const std::u32string& p) {
    if (p.empty()) return false;
    bool allUpper = true;
    for (char32_t c : p) allUpper &= isUpper(c);
    if (allUpper) return true;
    size_t i = isUpper(p[0]) ? 1 : 0;
    if (i >= p.size()) return false;
    for (; i < p.size(); ++i)
        if (!isLower(p[i])) return false;
    return true;
}

// Score of one token: WORD = lead* (letters (join letters)* | digits) trail*
int tokenScore(const std::u32string& tok) {
    size_t i = 0;
    const size_t n = tok.size();
    while (i < n && isLead(tok[i])) ++i;
    const size_t coreStart = i;
    bool digits = false;
    std::vector<std::u32string> parts;
    if (i < n && isLetter(tok[i])) {
        std::u32string part;
        while (i < n && isLetter(tok[i])) part.push_back(tok[i++]);
        parts.push_back(std::move(part));
        while (i + 1 < n && isJoin(tok[i]) && isLetter(tok[i + 1])) {
            ++i;
            part.clear();
            while (i < n && isLetter(tok[i])) part.push_back(tok[i++]);
            parts.push_back(std::move(part));
        }
    } else if (i < n && isDigit(tok[i])) {
        digits = true;
        while (i < n && isDigit(tok[i])) ++i;
    }
    const size_t coreEnd = i;
    bool ok = coreEnd > coreStart;
    while (ok && i < n) ok = isTrail(tok[i++]);
    if (ok && !digits) {
        for (const auto& p : parts) ok = ok && plainPart(p);
    }
    return ok ? int(coreEnd - coreStart) : -2 * int(n);
}

}  // namespace

int wordiness(std::string_view text) {
    int score = 0;
    for (const auto& tok : splitWords(decodeUtf8(text))) score += tokenScore(tok);
    return score;
}

std::string collapseSpaces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out.push_back(' ');
            pendingSpace = false;
            out.push_back(c);
        }
    }
    return out;
}

bool hasAlnum(std::string_view text) {
    for (char c : text)
        if (isAsciiAlnum(c)) return true;
    return false;
}

std::string repairReading(std::string_view input) {
    std::string text(input);
    // 1. A lone "l" (no letter/digit on either side) is "I": "l'm", "l will".
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != 'l') continue;
        const bool before = i > 0 && isAsciiAlnum(text[i - 1]);
        const bool after = i + 1 < text.size() && isAsciiAlnum(text[i + 1]);
        if (!before && !after) text[i] = 'I';
    }
    // 2. Junk before the first letter/digit/quote goes.
    size_t start = 0;
    while (start < text.size() && !isAsciiAlnum(text[start]) && text[start] != '"') ++start;
    text.erase(0, start);
    // 3. The same words twice -> once.
    std::vector<std::string> words;
    {
        std::string cur;
        for (char c : text) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
                if (!cur.empty()) words.push_back(std::move(cur)), cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) words.push_back(std::move(cur));
    }
    const size_t half = words.size() / 2;
    if (half >= 2 && words.size() % 2 == 0 && std::equal(words.begin(), words.begin() + half, words.begin() + half))
        words.resize(half);
    std::string out;
    for (const auto& w : words) {
        if (!out.empty()) out.push_back(' ');
        out += w;
    }
    return out;
}

}  // namespace crispplay::subs::ocr
