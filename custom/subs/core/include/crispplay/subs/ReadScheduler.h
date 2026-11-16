// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/ReadScheduler.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include "crispplay/subs/Reading.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

// Decision for one frame: reuse the last reading, or run OCR (`deep`: also try the slower
// zoomed search on an empty region).
struct ReadPlan {
    bool reuse = false;
    std::string text;
    bool deep = false;

    static ReadPlan reused(std::string t) { return {true, std::move(t), false}; }
    static ReadPlan read(bool deep) { return {false, {}, deep}; }
    bool operator==(const ReadPlan&) const = default;
};

// Picks the frames that need OCR. Most of the time the region is empty or unchanged. The
// signature is a small grey thumbnail of the region (CouchSubs: 64x12 bytes); the OCR stage
// computes it.
class ReadScheduler {
public:
    // Signature size: SignatureWidth grey samples per row, SignatureHeight rows.
    static constexpr int SignatureWidth = 64, SignatureHeight = 12;
    // Video noise stays below this difference; an added letter does not.
    static constexpr int ChangeThreshold = 16;

    explicit ReadScheduler(int changeThreshold = ChangeThreshold, int maxReuse = 20, int deepEvery = 3);
    // Every frame is read and empty frames are always read deep.
    static ReadScheduler always() { return ReadScheduler(-1, 0, 1); }

    ReadPlan plan(const std::vector<uint8_t>& signature);
    void record(const std::string& text);
    void reset();
    // Do two signatures differ (a sample changed by more than `threshold`)? With `within`, only
    // the samples that box covers (fractions of the region) count; movement elsewhere in the
    // region does not (CouchSubs ReadScheduler.Changed, f4e5284).
    static bool changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int threshold,
                        std::optional<TextBox> within = std::nullopt);
    // Signature of a BGRA image: SignatureWidth x SignatureHeight grey values (B + 2G + R) / 4.
    // CrispPlay: each value is the mean of its cell (CouchSubs samples one pixel per cell), so a
    // line's glyphs move it even when no sampled pixel would hit a stroke.
    static std::vector<uint8_t> signature(const uint8_t* bgra, int width, int height, int stride);

private:
    bool changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const;

    int changeThreshold_, maxReuse_, deepEvery_;
    std::optional<std::vector<uint8_t>> previous_;
    std::string lastText_;
    int reused_ = 0;
    int emptyReads_ = 0;
};

}  // namespace crispplay::subs
