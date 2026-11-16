// CrispPlay subtitle core: a speaker name on its own row above the dialogue, without a colon:
//
//               Brooding Old Archer
//   I'll mend this bow for you. Take it, and kill those who deserve it.
//
// The name is shown as ShownLine.speaker and never translated. ("NAME: text" labels are handled
// by SubtitleSegmenter / LineTranslator.) Part of CrispPlay, GPL-3.0 (see LICENSE).
#pragma once

#include "crispplay/subs/Reading.h"

#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

// Speakers found before (with clear evidence); a later, shorter dialogue under the same name, or
// a slightly misread name ("Olk MaE Tao"), is recognised through them.
class KnownSpeakers {
public:
    static constexpr size_t Capacity = 32;
    static constexpr double Similarity = 0.75;  // TextMatch ratio of the normalized names

    void add(const std::string& name);
    // The known spelling of a row that reads like a known name; nullopt if none.
    std::optional<std::string> match(const std::string& row) const;
    bool empty() const { return names_.empty(); }

private:
    std::vector<std::pair<std::string, std::string>> names_;  // normalized, as shown; newest last
};

struct SpeakerSplit {
    std::string speaker;  // the name as shown (known spelling if it matched one)
    Reading body;         // the dialogue rows
    bool certain = false; // geometry and text both say so: worth remembering
};

namespace SpeakerRow {

// 1-4 words in Title Case or ALL CAPS ("of", "the", "de"... may stay lower case after the first
// word), letters with ' and - only, no sentence punctuation (abbreviations like "Dr." pass).
bool looksLikeName(const std::string& row);

// The block's first row is a speaker name when it is a row of its own directly above the rest
// (gap under 1.2 row heights), centred over it, and either
//  - looks like a name and is clearly narrower than the dialogue (at most 70 % of its width;
//    dialogue wrapped over two rows fills the first row, so a short first row is no wrap;
//    a lower case second row additionally needs <= 55 %), or
//  - reads like a known speaker and has no sentence punctuation (any width: "Old Man Tao" over
//    "And you?").
// Lines without boxes (text-only readers) are never split.
std::optional<SpeakerSplit> split(const Reading& block, const KnownSpeakers& known);

// A block that is only a known speaker's name (its dialogue was not read this time).
bool isLoneSpeaker(const Reading& block, const KnownSpeakers& known);

}  // namespace SpeakerRow
}  // namespace crispplay::subs
