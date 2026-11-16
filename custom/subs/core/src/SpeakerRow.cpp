// CrispPlay subtitle core: speaker name row above the dialogue. Part of CrispPlay, GPL-3.0 (see LICENSE).
#include "crispplay/subs/SpeakerRow.h"

#include "crispplay/subs/SubtitleStabilizer.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <cmath>

namespace crispplay::subs {
namespace {

// Lower case particles of names ("Sword of the North", "Leonardo da Vinci").
const std::u32string_view kParticles[] = {U"of", U"the", U"de", U"da", U"del", U"della", U"di", U"du", U"von",
                                          U"van", U"der", U"den", U"al", U"el", U"la", U"le", U"bin", U"ibn", U"y",
                                          U"and", U"in"};
const std::u32string_view kAbbreviations[] = {U"Mr.", U"Mrs.", U"Ms.", U"Dr.", U"St.", U"Jr.", U"Sr.", U"Lt.", U"Sgt.",
                                              U"Capt.", U"Prof.", U"Col.", U"Gen.", U"Mt."};

bool isNameChar(char32_t c) { return uni::isLetter(c) || uni::isMark(c) || c == '\'' || c == U'’' || c == '-'; }

// Title Case word ("Archer", "McCoy", "O'Brien", "Thread-Binding") or ALL CAPS word.
bool capitalised(const std::u32string& word) {
    std::u32string letters;
    for (char32_t c : word)
        if (uni::isLetter(c)) letters.push_back(c);
    if (letters.empty() || !uni::isUpper(letters[0])) return false;
    const auto upper = std::count_if(letters.begin(), letters.end(), [](char32_t c) { return uni::isUpper(c); });
    if (static_cast<size_t>(upper) == letters.size()) return true;
    // Title Case: a few inner capitals (after Mc, O', a hyphen) at most.
    return upper <= 1 + static_cast<long>(std::count(word.begin(), word.end(), U'-')) + 1;
}

bool endsLikeSentence(const std::u32string& text) {
    return !text.empty() && uni::contains(U".!?…,;:", text.back());
}

double width(const TextBox& b) { return b.right - b.left; }
double middle(const TextBox& b) { return (b.left + b.right) / 2; }

}  // namespace

void KnownSpeakers::add(const std::string& name) {
    const auto key = TextMatch::normalize(name);
    if (key.size() < 2) return;
    names_.erase(std::remove_if(names_.begin(), names_.end(), [&](const auto& n) { return n.first == key; }), names_.end());
    names_.emplace_back(key, name);
    if (names_.size() > Capacity) names_.erase(names_.begin());
}

std::optional<std::string> KnownSpeakers::match(const std::string& row) const {
    const auto text = uni::trim(uni::decode(row));
    if (text.empty() || endsLikeSentence(text) || uni::splitWhiteSpace(text).size() > 5) return std::nullopt;
    std::vector<std::string> keys{TextMatch::normalize(row)};
    // A scrap read in front of the name ("4 ruo Mel") is tried without it too.
    if (const auto words = uni::splitWhiteSpace(text); words.size() >= 2 && words[0].size() <= 2) {
        std::vector<std::u32string> rest(words.begin() + 1, words.end());
        keys.push_back(TextMatch::normalize(uni::encode(uni::join(rest, U" "))));
    }
    for (const auto& key : keys) {
        if (key.size() < 2) continue;
        // Newest first: the speaker of the last lines is the likeliest.
        for (auto it = names_.rbegin(); it != names_.rend(); ++it)
            if (it->first == key) return it->second;
        for (auto it = names_.rbegin(); it != names_.rend(); ++it)
            if (TextMatch::isSimilar(key, it->first, Similarity)) return it->second;
    }
    return std::nullopt;
}

namespace SpeakerRow {

bool looksLikeName(const std::string& rowUtf8) {
    const auto row = uni::trim(uni::decode(rowUtf8));
    if (row.empty()) return false;
    for (char32_t c : row)
        if (uni::contains(U",;:!?…\"“”()[]{}*/\\|", c) || uni::isDigit(c)) return false;
    const auto words = uni::splitWhiteSpace(row);
    if (words.empty() || words.size() > 4) return false;
    size_t letters = 0;
    for (size_t i = 0; i < words.size(); ++i) {
        auto word = words[i];
        if (std::any_of(std::begin(kAbbreviations), std::end(kAbbreviations), [&](auto a) { return word == a; })) {
            letters += word.size() - 1;
            continue;
        }
        if (!std::all_of(word.begin(), word.end(), isNameChar)) return false;
        const auto count = std::count_if(word.begin(), word.end(), [](char32_t c) { return uni::isLetter(c); });
        if (count == 0) return false;
        letters += static_cast<size_t>(count);
        if (i > 0 && std::any_of(std::begin(kParticles), std::end(kParticles), [&](auto p) { return word == p; })) continue;
        if (!capitalised(word)) return false;
    }
    return letters >= 2;
}

std::optional<SpeakerSplit> split(const Reading& block, const KnownSpeakers& known) {
    const auto& lines = block.lines();
    if (lines.size() < 2) return std::nullopt;
    if (std::any_of(lines.begin(), lines.end(), [](const TextLine& l) { return !l.box; })) return std::nullopt;
    const TextLine& top = lines[0];
    const TextBox& name = *top.box;
    TextBox rest = *lines[1].box;
    for (size_t i = 2; i < lines.size(); ++i) rest = rest.unite(*lines[i].box);
    const TextBox& below = *lines[1].box;

    // A row of its own, right above the dialogue.
    const double nameH = name.bottom - name.top, belowH = below.bottom - below.top;
    if (nameH <= 0 || belowH <= 0) return std::nullopt;
    // (The dialogue's box may reach up when OCR merged noise into it: its middle must be below
    // the name and its top below the name's middle.)
    if ((below.top + below.bottom) / 2 < name.bottom || below.top < name.top + 0.5 * nameH) return std::nullopt;
    if (below.top - name.bottom > 1.2 * std::max(nameH, belowH)) return std::nullopt;
    // Centred over the dialogue (a known name: or centred in the band, over a partly read row).
    const double restW = width(rest), nameW = width(name);
    if (restW <= 0 || nameW <= 0) return std::nullopt;
    const auto known_ = known.match(top.text);
    const bool overRest = std::abs(middle(name) - middle(rest)) <= std::max(0.15 * restW, 0.5 * nameW);
    if (!overRest && !(known_ && std::abs(middle(name) - 0.5) <= 0.06)) return std::nullopt;

    std::vector<TextLine> body(lines.begin() + 1, lines.end());
    const bool nameLike = looksLikeName(top.text);
    const double ratio = nameW / restW;
    bool lowerNext = false;
    for (char32_t c : uni::decode(lines[1].text))
        if (uni::isLetter(c)) { lowerNext = uni::isLower(c); break; }
    if (overRest && nameLike && ratio <= 0.7 && (!lowerNext || ratio <= 0.55))
        return SpeakerSplit{known_ ? *known_ : uni::encode(uni::trim(uni::decode(top.text))), Reading(std::move(body)), true};
    if (known_) return SpeakerSplit{*known_, Reading(std::move(body)), false};
    return std::nullopt;
}

bool isLoneSpeaker(const Reading& block, const KnownSpeakers& known) {
    return block.lines().size() == 1 && known.match(block.lines()[0].text).has_value();
}

}  // namespace SpeakerRow
}  // namespace crispplay::subs
