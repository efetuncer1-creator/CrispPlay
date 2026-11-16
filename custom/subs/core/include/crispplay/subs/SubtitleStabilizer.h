// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitleStabilizer.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include "crispplay/subs/Reading.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

enum class StabilizerKind { None, Translate, Clear, Restore };

// What the reading pipeline does with one frame.
struct StabilizerAction {
    StabilizerKind kind = StabilizerKind::None;
    std::string text;

    static StabilizerAction none() { return {}; }
    static StabilizerAction clear() { return {StabilizerKind::Clear, {}}; }
    static StabilizerAction translate(std::string t) { return {StabilizerKind::Translate, std::move(t)}; }
    // A line that just left came back: show its translation again (text: first sent form).
    static StabilizerAction restore(std::string t) { return {StabilizerKind::Restore, std::move(t)}; }
    bool operator==(const StabilizerAction&) const = default;
};

// Follows OCR output frame by frame; asks for a translation once a subtitle is stable and for
// a clear once it has left. Never sends the same subtitle twice. Decisions follow a clock
// (milliseconds), not the read rate.
class SubtitleStabilizer {
public:
    // The region must stay empty this long before the subtitle counts as gone.
    static constexpr int ClearAfterMs = 800;
    // A line coming back within this time is the same line (reads were missed).
    static constexpr int RestoreWithinMs = 1500;
    // A line whose translation failed is retried at most this often.
    static constexpr int RetryAfterMs = 1000;
    // A garbled-looking reading goes only after staying the same this long. CrispPlay: so does
    // a broken one (OcrNoise::looksBroken: "nd kill shose wh"), and of the similar readings
    // seen meanwhile the best one (OcrNoise::quality) is sent; a clean reading that comes after
    // a broken one was sent is sent again (a revision of the line).
    static constexpr int GarbledStableMs = 700;
    // An empty reading while the line's place has not changed since it was last read (region
    // signature, ReadScheduler::changed) means OCR missed it: white text on a bright background
    // can go unread for seconds and the translation was cleared while the English stayed
    // (CouchSubs f4e5284, It Takes Two). Once the place changes the usual ClearAfterMs counts;
    // an unchanged place keeps the line at most this long.
    static constexpr int HoldUnchangedMs = 4000;

    using Clock = std::function<int64_t()>;
    explicit SubtitleStabilizer(Clock clock = {}, double similarity = 0.85, int stableFrames = 2,
                                int clearAfterMs = ClearAfterMs);

    // `signature`: the frame's region signature (ReadScheduler); `box`: where the read text lies
    // in the region (fractions). Without them an empty reading always counts as the line leaving.
    StabilizerAction feed(const std::string& rawText, const std::vector<uint8_t>* signature = nullptr,
                          std::optional<TextBox> box = std::nullopt);
    // Ends with . ! ? or … (closing quotes and brackets ignored).
    static bool endsSentence(const std::string& raw);
    // A translation nearly equal to its source means the text is already in the target language.
    bool shouldSend(const std::string& source, const std::string& translation) const;
    // Forget the last sent line (its translation failed); it is translated again while it
    // stays, at most every RetryAfterMs.
    void forgetSent();
    // CrispPlay: the sent line's translation was not shown (English left in it): a better
    // reading of the same line is sent again, like after a broken reading.
    void sentUnusable() { if (!sent_.empty()) sentBroken_ = true; }
    StabilizerAction reset();

private:
    struct Left { std::string rows, raw; int64_t leftAt; };
    void leaveScreen(int64_t time);
    static bool isSame(const std::string& text, const std::string& rows);
    // The line on screen was read in this frame: keep the signature and the line's place.
    void remember(const std::vector<uint8_t>* signature, const std::optional<TextBox>& box);
    bool looksUnchanged(const std::vector<uint8_t>* signature) const;

    Clock now_;
    double similarity_;
    int stableFrames_;
    int clearAfterMs_;
    std::string candidate_;
    int candidateCount_ = 0;
    int64_t candidateSince_ = 0;
    std::string bestRaw_;  // best reading of the candidate so far (CrispPlay)
    int bestQuality_ = 0;
    std::string sent_, sentRaw_;  // normalized rows and first read form of the line on screen
    bool sentBroken_ = false;     // sentRaw_ looked broken: a clean reading revises it
    int sentQuality_ = 0;
    std::optional<int64_t> emptySince_;
    std::optional<int64_t> goneSince_;  // the empty region's line place changed (or is unknown)
    struct Seen { std::vector<uint8_t> signature; TextBox box; };
    std::optional<Seen> lastSeen_;      // frame where the line on screen was last read
    std::vector<Left> history_;
    std::string forgotten_;
    int64_t retryAt_ = 0;
};

}  // namespace crispplay::subs
