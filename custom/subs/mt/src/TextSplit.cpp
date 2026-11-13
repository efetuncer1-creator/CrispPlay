#include <crispplay/subs/mt/TextSplit.h>

#include <array>

namespace crispplay::subs::mt {
namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6"; // U+2026
constexpr std::array<std::string_view, 8> kAbbreviations = {"Mr.", "Mrs.", "Ms.", "Dr.", "St.", "Sgt.", "Lt.", "Capt."};

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Length of the sentence-final punctuation unit at i (. ! ? or the ellipsis), 0 if none.
std::size_t punctAt(std::string_view s, std::size_t i)
{
    const char c = s[i];
    if (c == '.' || c == '!' || c == '?') return 1;
    if (s.substr(i, kEllipsis.size()) == kEllipsis) return kEllipsis.size();
    return 0;
}

bool endsWithAbbreviation(std::string_view head)
{
    for (const auto a : kAbbreviations) {
        if (head.size() >= a.size() && head.substr(head.size() - a.size()) == a) return true;
    }
    return false;
}

// Mirrors the regex [.!?…]+["')]*\s+(?=["'(]*[A-Z0-9]) anchored at i; returns the
// match end or npos.
std::size_t matchBreak(std::string_view s, std::size_t i)
{
    std::size_t j = i;
    std::size_t n = 0;
    while (j < s.size() && (n = punctAt(s, j)) != 0) j += n;
    if (j == i) return std::string_view::npos;
    while (j < s.size() && (s[j] == '"' || s[j] == '\'' || s[j] == ')')) ++j;
    const std::size_t spaceStart = j;
    while (j < s.size() && isSpace(s[j])) ++j;
    if (j == spaceStart) return std::string_view::npos;
    std::size_t k = j;
    while (k < s.size() && (s[k] == '"' || s[k] == '\'' || s[k] == '(')) ++k;
    if (k >= s.size()) return std::string_view::npos;
    const char c = s[k];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return std::string_view::npos;
    return j;
}

} // namespace

std::string_view trim(std::string_view text)
{
    std::size_t b = 0;
    std::size_t e = text.size();
    while (b < e && isSpace(text[b])) ++b;
    while (e > b && isSpace(text[e - 1])) --e;
    return text.substr(b, e - b);
}

std::vector<std::string> splitSentences(std::string_view text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t end = punctAt(text, i) ? matchBreak(text, i) : std::string_view::npos;
        if (end == std::string_view::npos) {
            ++i;
            continue;
        }
        const std::string_view head = trim(text.substr(start, end - start));
        i = end; // finditer resumes after the match either way
        if (endsWithAbbreviation(head)) continue;
        parts.emplace_back(head);
        start = end;
    }
    const std::string_view tail = trim(text.substr(start));
    if (!tail.empty()) parts.emplace_back(tail);
    return parts;
}

std::string joinSentences(const std::vector<std::string>& parts)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ' ';
        out += parts[i];
    }
    return out;
}

std::string truncateUtf8(std::string_view text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes) return std::string(text);
    std::size_t cut = maxBytes;
    // Back off continuation bytes (10xxxxxx) so the cut lands on a code point start.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    const std::size_t floor = cut - cut / 4;
    for (std::size_t k = cut; k > floor; --k) {
        if (isSpace(text[k - 1])) {
            cut = k - 1;
            break;
        }
    }
    return std::string(trim(text.substr(0, cut)));
}

bool containsUnknownMarker(std::string_view translated)
{
    return translated.find(kUnknownMarker) != std::string_view::npos;
}

} // namespace crispplay::subs::mt
