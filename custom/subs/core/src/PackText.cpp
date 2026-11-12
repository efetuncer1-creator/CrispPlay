// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/GamePacks.cs (PackKey),
// PackFuzzy.cs and windows/src/CouchSubs.Translation/PackLookup.cs (LooseLabel).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/PackText.h"

#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace crispplay::subs {
namespace {

using uni::Cat;

#ifdef _WIN32
std::u32string normalize(const std::u32string& text, NORM_FORM form) {
    if (text.empty()) return text;
    std::wstring wide;
    for (char32_t c : text) {
        if (c >= 0x10000) {
            wide.push_back(static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10)));
            wide.push_back(static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            wide.push_back(static_cast<wchar_t>(c));
        }
    }
    int size = NormalizeString(form, wide.data(), static_cast<int>(wide.size()), nullptr, 0);
    for (int attempt = 0; attempt < 4 && size > 0; ++attempt) {
        std::wstring out(static_cast<size_t>(size), L'\0');
        const int written = NormalizeString(form, wide.data(), static_cast<int>(wide.size()), out.data(), size);
        if (written > 0) {
            out.resize(static_cast<size_t>(written));
            std::u32string result;
            for (size_t i = 0; i < out.size(); ++i) {
                const char32_t c = out[i];
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < out.size()) {
                    result.push_back(0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(out[++i]) - 0xDC00));
                } else {
                    result.push_back(c);
                }
            }
            return result;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        size = -written;
    }
    return text;
}
std::u32string nfkc(const std::u32string& t) { return normalize(t, NormalizationKC); }
std::u32string nfd(const std::u32string& t) { return normalize(t, NormalizationD); }
#else
std::u32string nfkc(const std::u32string& t) { return t; }
std::u32string nfd(const std::u32string& t) { return t; }
#endif

constexpr int kMaxTagLength = 40;

std::u32string stripTags32(const std::u32string& text, const std::u32string& replacement) {
    if (!uni::contains(text, U'<')) return text;
    std::u32string out;
    size_t index = 0;
    while (index < text.size()) {
        if (text[index] == U'<') {
            size_t end = index + 1;
            while (end < text.size() && end - index - 1 <= kMaxTagLength && text[end] != U'<' && text[end] != U'>') ++end;
            if (end < text.size() && text[end] == U'>' && end - index - 1 <= kMaxTagLength) {
                out += replacement;
                index = end + 1;
                continue;
            }
        }
        out.push_back(text[index++]);
    }
    return out;
}

std::u32string canon32(const std::string& text) {
    // .NET's invariant lower case leaves "İ" alone; Python and Swift give "i" + dot above (the dot
    // goes in step 4). Same result: that mapping first.
    auto lowered = uni::toLowerInvariant(uni::replaceAll(nfkc(stripTags32(uni::decode(text), U" ")), U"İ", U"i̇"));
    std::u32string out;
    bool gap = false;
    for (char32_t c : nfd(lowered)) {
        switch (uni::category(c)) {
        case Cat::Mn: case Cat::Mc: case Cat::Me:
            continue;
        case Cat::Lu: case Cat::Ll: case Cat::Lt: case Cat::Lm: case Cat::Lo: case Cat::Nd: case Cat::Nl: case Cat::No:
            if (gap && !out.empty()) out.push_back(U' ');
            gap = false;
            out.push_back(c);
            break;
        default:
            gap = true;
        }
    }
    return out;
}

// --- SHA-256 (FIPS 180-4) ---
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    void block(const unsigned char* p) {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
};

const std::u32string_view kEdge = U".,!?;:'\"()[]-…“”’‘";
const std::u32string_view kTypographic = U"’‘“”—–…";

// CharUnicodeInfo.GetDigitValue >= 0: decimal digits plus the digit-valued No characters.
bool isDigitValue(char32_t c) {
    if (c >= 0x10000) return false;
    if (uni::isDigit(c)) return true;
    return c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0x2070 && c <= 0x2079 && c != 0x2071 && c != 0x2072 && c != 0x2073) ||
           (c >= 0x2080 && c <= 0x2089) || (c >= 0x2460 && c <= 0x2468) || (c >= 0x2474 && c <= 0x247C) ||
           (c >= 0x2488 && c <= 0x2490) || c == 0x24EA || (c >= 0x24F5 && c <= 0x24FD) || c == 0x24FF ||
           (c >= 0x2776 && c <= 0x277E) || (c >= 0x2780 && c <= 0x2788) || (c >= 0x278A && c <= 0x2792);
}

}  // namespace

namespace PackKey {

std::string stripTags(const std::string& text, const std::string& replacement) {
    return uni::encode(stripTags32(uni::decode(text), uni::decode(replacement)));
}

std::string canon(const std::string& text) { return uni::encode(canon32(text)); }

std::string sha256Hex(const std::string& bytes) {
    Sha256 sha;
    std::string data = bytes;
    const uint64_t bitLength = static_cast<uint64_t>(bytes.size()) * 8;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back('\0');
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<char>((bitLength >> (8 * i)) & 0xFF));
    for (size_t i = 0; i < data.size(); i += 64) sha.block(reinterpret_cast<const unsigned char*>(data.data() + i));
    std::string out;
    char buf[9];
    for (uint32_t v : sha.h) {
        std::snprintf(buf, sizeof buf, "%08x", v);
        out += buf;
    }
    return out;
}

std::optional<std::string> key(const std::string& text) {
    const auto c = canon(text);
    if (c.empty()) return std::nullopt;
    return sha256Hex(c).substr(0, 16);
}

}  // namespace PackKey

namespace PackFuzzy {

std::u32string folded(const std::string& text) {
    std::u32string out;
    for (char32_t c : canon32(text)) {
        if (c == U' ') continue;
        switch (c) {
        case U'l': case U'1': case U'ı': case U'j': c = U'i'; break;
        case U'0': case U'ø': case U'œ': c = U'o'; break;
        case U'5': case U'ß': c = U's'; break;
        case U'æ': c = U'a'; break;
        default: break;
        }
        out.push_back(c);
    }
    return out;
}

int bits(int length) {
    int b = 64;
    while (b < MaxBits && length * 3 > b) b *= 2;
    return b;
}

std::vector<uint64_t> bloom(const std::u32string& runes, int bitCount) {
    std::set<uint64_t> hashes;
    for (size_t i = 0; i + 3 <= runes.size(); ++i) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (unsigned char b : uni::encode(runes.substr(i, 3))) {
            hash ^= b;
            hash *= 0x100000001b3ULL;
        }
        hashes.insert(hash);
    }
    std::vector<uint64_t> words(static_cast<size_t>(bitCount / 64));
    for (uint64_t hash : hashes) {
        const auto bit = static_cast<int>(hash & static_cast<uint64_t>(bitCount - 1));
        words[bit >> 6] |= 1ULL << (bit & 63);
    }
    return words;
}

std::optional<std::pair<int, std::string>> signature(const std::string& text) {
    const auto runes = folded(text);
    if (runes.size() < MinLength) return std::nullopt;
    const int length = static_cast<int>(std::min<size_t>(runes.size(), 255));
    std::string hex;
    char buf[3];
    for (uint64_t word : bloom(runes, bits(length)))
        for (int b = 0; b < 8; ++b) {
            std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>((word >> (8 * b)) & 0xFF));
            hex += buf;
        }
    return std::make_pair(length, hex);
}

double score(int both, int inQuery, int inEntry, int bitCount) {
    if (inQuery == 0 || inEntry == 0) return 0;
    const double fillQuery = static_cast<double>(inQuery) / bitCount, fillEntry = static_cast<double>(inEntry) / bitCount;
    if (fillQuery >= 1 || fillEntry >= 1) return 0;
    const double queryCovered = (static_cast<double>(both) / inQuery - fillEntry) / (1 - fillEntry);
    const double entryCovered = (static_cast<double>(both) / inEntry - fillQuery) / (1 - fillQuery);
    return 100.0 * std::min(queryCovered, entryCovered);
}

double similarity(const std::vector<uint64_t>& query, const std::vector<uint64_t>& entry, int bitCount) {
    int both = 0, inQuery = 0, inEntry = 0;
    for (size_t i = 0; i < static_cast<size_t>(bitCount / 64) && i < query.size() && i < entry.size(); ++i) {
        both += std::popcount(query[i] & entry[i]);
        inQuery += std::popcount(query[i]);
        inEntry += std::popcount(entry[i]);
    }
    return score(both, inQuery, inEntry, bitCount);
}

double required(int length, bool isDamaged) {
    static const std::pair<int, double> clean[] = {{24, 90}, {256, 80}};
    static const std::pair<int, double> damagedTable[] = {{24, 70}, {40, 60}, {256, 55}};
    if (isDamaged) {
        for (auto [limit, value] : damagedTable)
            if (length < limit) return value;
        return 55;
    }
    for (auto [limit, value] : clean)
        if (length < limit) return value;
    return 80;
}

bool damaged(const std::string& textUtf8) {
    const auto text = uni::decode(textUtf8);
    for (char32_t c : text)
        if (c > 127 && !uni::contains(kTypographic, c)) return true;
    for (const auto& word : uni::splitWhiteSpace(text)) {
        size_t a = 0, b = word.size();
        while (a < b && uni::contains(kEdge, word[a])) ++a;
        while (b > a && uni::contains(kEdge, word[b - 1])) --b;
        const std::u32string core = word.substr(a, b - a);
        if (core.empty()) continue;
        for (size_t i = 1; i + 1 < core.size(); ++i)
            if (uni::isLetter(core[i - 1]) && uni::isLetter(core[i + 1]) && !uni::isLetter(core[i]) && core[i] != U'\'' &&
                core[i] != U'’' && core[i] != U'-')
                return true;
        for (size_t i = 1; i < core.size(); ++i)
            if ((uni::isLetter(core[i - 1]) && isDigitValue(core[i])) || (isDigitValue(core[i - 1]) && uni::isLetter(core[i])))
                return true;
        if (core.size() == 1 && uni::isLetter(core[0]) && !uni::contains(U"aAIoO", core[0])) return true;
        if (core.size() > 14) return true;
    }
    return false;
}

}  // namespace PackFuzzy

std::optional<std::pair<std::string, std::string>> looseLabel(const std::string& lineUtf8) {
    const auto runes = uni::decode(lineUtf8);
    auto nameLike = [](const std::u32string& token, size_t minimum) {
        size_t letters = 0;
        for (char32_t c : token) {
            if (!uni::isLetter(c)) continue;
            if (uni::isLower(c)) return false;
            ++letters;
        }
        return letters >= minimum;
    };
    for (size_t index = 1; index < std::min<size_t>(runes.size(), 33); ++index) {
        if (runes[index] != U':' && runes[index] != U'•') continue;
        const auto tokens = uni::splitWhiteSpace(runes.substr(0, index));
        if (tokens.empty() || !nameLike(tokens.back(), 1)) continue;
        size_t start = index + 1;
        while (start < runes.size() && !uni::isLetter(runes[start]) && !uni::isNumber(runes[start])) ++start;
        const auto body = uni::trim(runes.substr(start));
        if (!uni::anyLetter(body)) return std::nullopt;
        return std::make_pair(uni::encode(uni::trim(runes.substr(0, index + 1)) + U" "), uni::encode(body));
    }
    size_t position = 0;
    while (position < runes.size() && !uni::isLetter(runes[position])) ++position;
    for (int word = 0; word < 3; ++word) {
        size_t end = position;
        while (end < runes.size() && !uni::isWhiteSpace(runes[end])) ++end;
        if (end >= runes.size() || !uni::isUpper(runes[position]) || !nameLike(runes.substr(position, end - position), 2)) break;
        while (end < runes.size() && uni::isWhiteSpace(runes[end])) ++end;
        position = end;
        if (position + 1 < runes.size() && uni::isUpper(runes[position]) && uni::isLower(runes[position + 1]))
            return std::make_pair(uni::encode(uni::trim(runes.substr(0, position)) + U" "), uni::encode(uni::trim(runes.substr(position))));
    }
    return std::nullopt;
}

}  // namespace crispplay::subs
