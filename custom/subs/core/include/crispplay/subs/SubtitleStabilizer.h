// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitleStabilizer.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

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
    // A garbled-looking reading goes only after staying the same this long.
    static constexpr int GarbledStableMs = 700;

    using Clock = std::function<int64_t()>;
    explicit SubtitleStabilizer(Clock clock = {}, double similarity = 0.85, int stableFrames = 2,
                                int clearAfterMs = ClearAfterMs);

    StabilizerAction feed(const std::string& rawText);
    // Ends with . ! ? or … (closing quotes and brackets ignored).
    static bool endsSentence(const std::string& raw);
    // A translation nearly equal to its source means the text is already in the target language.
    bool shouldSend(const std::string& source, const std::string& translation) const;
    // Forget the last sent line (its translation failed); it is translated again while it
    // stays, at most every RetryAfterMs.
    void forgetSent();
    StabilizerAction reset();

private:
    struct Left { std::string rows, raw; int64_t leftAt; };
    void leaveScreen(int64_t time);
    static bool isSame(const std::string& text, const std::string& rows);

    Clock now_;
    double similarity_;
    int stableFrames_;
    int clearAfterMs_;
    std::string candidate_;
    int candidateCount_ = 0;
    int64_t candidateSince_ = 0;
    std::string sent_, sentRaw_;  // normalized rows and first read form of the line on screen
    std::optional<int64_t> emptySince_;
    std::vector<Left> history_;
    std::string forgotten_;
    int64_t retryAt_ = 0;
};

}  // namespace crispplay::subs
