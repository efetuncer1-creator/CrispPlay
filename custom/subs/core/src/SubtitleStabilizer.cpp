// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitleStabilizer.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/SubtitleStabilizer.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/ReadScheduler.h"
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

StabilizerAction SubtitleStabilizer::feed(const std::string& rawText, const std::vector<uint8_t>* signature,
                                          std::optional<TextBox> box) {
    const int64_t time = now_();
    const std::string text = TextMatch::normalize(rawText);
    // Readings shorter than 3 characters (noise, a lone symbol) count as an empty frame.
    if (uni::decode(text).size() < 3) {
        candidate_.clear();
        candidateCount_ = 0;
        if (!emptySince_) emptySince_ = time;
        if (sent_.empty()) return StabilizerAction::none();
        // The line's place unchanged since it was last read: OCR missed it, it has not left.
        if (!goneSince_ && !looksUnchanged(signature)) goneSince_ = time;
        if ((goneSince_ && time - *goneSince_ >= clearAfterMs_) || time - *emptySince_ >= HoldUnchangedMs) {
            leaveScreen(time);
            return StabilizerAction::clear();
        }
        return StabilizerAction::none();
    }

    emptySince_.reset();
    goneSince_.reset();
    const int quality = OcrNoise::quality(rawText);
    if (TextMatch::isSimilar(text, candidate_, similarity_)) {
        ++candidateCount_;
        // CrispPlay: of the similar readings the best one counts (a line partly read over a
        // bright background is read better in another sample).
        if (quality > bestQuality_) {
            bestRaw_ = rawText;
            bestQuality_ = quality;
        }
    } else {
        candidate_ = text;
        candidateCount_ = 1;
        candidateSince_ = time;
        bestRaw_ = rawText;
        bestQuality_ = quality;
    }
    const std::string raw = bestRaw_;
    const std::string best = TextMatch::normalize(raw);
    // A line ending with punctuation is complete and goes on the first frame; a garbled one
    // (subtitles cross-fading) waits until it has been stable longer than a cross-fade.
    // CrispPlay: so does a broken one ("Take it, nd kill shose wh"): a better reading may come.
    if (isSame(TextMatch::normalize(rawText), sent_)) remember(signature, box);
    const bool broken = OcrNoise::looksBroken(raw);
    const bool ready = broken ? candidateCount_ >= stableFrames_ && time - candidateSince_ >= GarbledStableMs
                              : candidateCount_ >= stableFrames_ || endsSentence(raw);
    if (!ready) return StabilizerAction::none();
    // CrispPlay: a clean reading of a line that was sent from a broken reading is sent again.
    const bool revises = sentBroken_ && !broken && bestQuality_ > sentQuality_ && isSame(best, sent_);
    if (!revises) {
        // A noisy reread of the sent line is not a new line.
        if (isSame(best, sent_)) return StabilizerAction::none();
        // Neither is a worse reread with a word or two read differently.
        if (!sentRaw_.empty() && TextMatch::isWorseReread(raw, sentRaw_)) {
            remember(signature, box);
            return StabilizerAction::none();
        }

        // A line that just left came back: reads were missed; show the old translation again.
        for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
            if (time - history_[i].leftAt <= RestoreWithinMs && isSame(best, history_[i].rows)) {
                Left back = history_[i];
                history_.erase(history_.begin() + i);
                leaveScreen(time);
                sent_ = back.rows;
                sentRaw_ = back.raw;
                sentBroken_ = OcrNoise::looksBroken(sentRaw_);
                sentQuality_ = OcrNoise::quality(sentRaw_);
                remember(signature, box);
                return StabilizerAction::restore(back.raw);
            }
        }
        // A line whose translation just failed is not asked for on every frame.
        if (!forgotten_.empty() && time < retryAt_ && isSame(best, forgotten_)) return StabilizerAction::none();
    }

    leaveScreen(time);
    forgotten_.clear();
    sentRaw_ = uni::encode(uni::trim(uni::decode(raw)));
    sent_ = TextMatch::normalizeRows(sentRaw_);
    sentBroken_ = broken;
    sentQuality_ = bestQuality_;
    remember(signature, box);
    return StabilizerAction::translate(sentRaw_);
}

void SubtitleStabilizer::remember(const std::vector<uint8_t>* signature, const std::optional<TextBox>& box) {
    if (signature && !signature->empty() && box) lastSeen_ = Seen{*signature, *box};
    else lastSeen_.reset();
}

bool SubtitleStabilizer::looksUnchanged(const std::vector<uint8_t>* signature) const {
    return signature && !signature->empty() && lastSeen_ &&
           !ReadScheduler::changed(lastSeen_->signature, *signature, ReadScheduler::ChangeThreshold, lastSeen_->box);
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
    sentBroken_ = false;
    lastSeen_.reset();
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
    sentBroken_ = false;
    lastSeen_.reset();
    candidate_.clear();
    candidateCount_ = 0;
}

StabilizerAction SubtitleStabilizer::reset() {
    candidate_.clear();
    candidateCount_ = 0;
    emptySince_.reset();
    goneSince_.reset();
    lastSeen_.reset();
    history_.clear();
    forgotten_.clear();
    if (sent_.empty()) return StabilizerAction::none();
    sent_.clear();
    sentRaw_.clear();
    sentBroken_ = false;
    return StabilizerAction::clear();
}

}  // namespace crispplay::subs
