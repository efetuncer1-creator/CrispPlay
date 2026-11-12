// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitleStabilizer.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/SubtitleStabilizer.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/Unicode.h"

#include <chrono>

namespace crispplay::subs {
namespace {
constexpr size_t kHistorySize = 5;

int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

SubtitleStabilizer::SubtitleStabilizer(Clock clock, double similarity, int stableFrames, int clearAfterMs)
    : now_(clock ? std::move(clock) : Clock(steadyMs)), similarity_(similarity), stableFrames_(stableFrames),
      clearAfterMs_(clearAfterMs) {}

StabilizerAction SubtitleStabilizer::feed(const std::string& rawText) {
    const int64_t time = now_();
    const std::string text = TextMatch::normalize(rawText);
    // Readings shorter than 3 characters (noise, a lone symbol) count as an empty frame.
    if (uni::decode(text).size() < 3) {
        candidate_.clear();
        candidateCount_ = 0;
        if (!emptySince_) emptySince_ = time;
        if (!sent_.empty() && time - *emptySince_ >= clearAfterMs_) {
            leaveScreen(time);
            return StabilizerAction::clear();
        }
        return StabilizerAction::none();
    }

    emptySince_.reset();
    if (TextMatch::isSimilar(text, candidate_, similarity_)) {
        ++candidateCount_;
    } else {
        candidate_ = text;
        candidateCount_ = 1;
        candidateSince_ = time;
    }
    // A line ending with punctuation is complete and goes on the first frame; a garbled one
    // (subtitles cross-fading) waits until it has been stable longer than a cross-fade.
    const bool ready = OcrNoise::looksGarbled(rawText)
                           ? candidateCount_ >= stableFrames_ && time - candidateSince_ >= GarbledStableMs
                           : candidateCount_ >= stableFrames_ || endsSentence(rawText);
    // A noisy reread of the sent line is not a new line.
    if (!ready || isSame(text, sent_)) return StabilizerAction::none();
    // Neither is a worse reread with a word or two read differently.
    if (!sentRaw_.empty() && TextMatch::isWorseReread(rawText, sentRaw_)) return StabilizerAction::none();

    // A line that just left came back: reads were missed; show the old translation again.
    for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
        if (time - history_[i].leftAt <= RestoreWithinMs && isSame(text, history_[i].rows)) {
            Left back = history_[i];
            history_.erase(history_.begin() + i);
            leaveScreen(time);
            sent_ = back.rows;
            sentRaw_ = back.raw;
            return StabilizerAction::restore(back.raw);
        }
    }
    // A line whose translation just failed is not asked for on every frame.
    if (!forgotten_.empty() && time < retryAt_ && isSame(text, forgotten_)) return StabilizerAction::none();

    leaveScreen(time);
    forgotten_.clear();
    sentRaw_ = uni::encode(uni::trim(uni::decode(rawText)));
    sent_ = TextMatch::normalizeRows(sentRaw_);
    return StabilizerAction::translate(sentRaw_);
}

bool SubtitleStabilizer::isSame(const std::string& text, const std::string& rows) {
    return !rows.empty() && TextMatch::isSameSubtitle(text, rows);
}

void SubtitleStabilizer::leaveScreen(int64_t time) {
    if (sent_.empty()) return;
    history_.push_back({sent_, sentRaw_, time});
    if (history_.size() > kHistorySize) history_.erase(history_.begin());
    sent_.clear();
    sentRaw_.clear();
}

bool SubtitleStabilizer::endsSentence(const std::string& raw) {
    auto text = uni::trim(uni::decode(raw));
    while (!text.empty() && uni::contains(U"\"'”’»)]", text.back())) text.pop_back();
    return !text.empty() && uni::contains(U".!?…", text.back());
}

bool SubtitleStabilizer::shouldSend(const std::string& source, const std::string& translation) const {
    return !TextMatch::isSimilar(TextMatch::normalize(source), TextMatch::normalize(translation), 0.90);
}

void SubtitleStabilizer::forgetSent() {
    if (!sent_.empty()) forgotten_ = sent_;
    retryAt_ = now_() + RetryAfterMs;
    sent_.clear();
    sentRaw_.clear();
    candidate_.clear();
    candidateCount_ = 0;
}

StabilizerAction SubtitleStabilizer::reset() {
    candidate_.clear();
    candidateCount_ = 0;
    emptySince_.reset();
    history_.clear();
    forgotten_.clear();
    if (sent_.empty()) return StabilizerAction::none();
    sent_.clear();
    sentRaw_.clear();
    return StabilizerAction::clear();
}

}  // namespace crispplay::subs
