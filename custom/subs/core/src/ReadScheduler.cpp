// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/ReadScheduler.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/ReadScheduler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace crispplay::subs {

ReadScheduler::ReadScheduler(int changeThreshold, int maxReuse, int deepEvery)
    : changeThreshold_(changeThreshold), maxReuse_(maxReuse), deepEvery_(deepEvery < 1 ? 1 : deepEvery) {}

ReadPlan ReadScheduler::plan(const std::vector<uint8_t>& signature) {
    auto before = std::move(previous_);
    previous_ = signature;
    if (before && reused_ < maxReuse_ && !changed(*before, signature)) {
        ++reused_;
        return ReadPlan::reused(lastText_);
    }
    reused_ = 0;
    return ReadPlan::read(emptyReads_ % deepEvery_ == 0);
}

void ReadScheduler::record(const std::string& text) {
    lastText_ = text;
    // When text has just left, the deep read is not due at once.
    emptyReads_ = text.empty() ? emptyReads_ + 1 : 1;
}

void ReadScheduler::reset() {
    previous_.reset();
    lastText_.clear();
    reused_ = 0;
    emptyReads_ = 0;
}

std::vector<uint8_t> ReadScheduler::signature(const uint8_t* bgra, int width, int height, int stride) {
    std::vector<uint8_t> samples(static_cast<size_t>(SignatureWidth) * SignatureHeight, 0);
    if (!bgra || width <= 0 || height <= 0 || stride < width * 4) return samples;
    for (int cy = 0; cy < SignatureHeight; ++cy) {
        const int y0 = cy * height / SignatureHeight, y1 = std::max(y0 + 1, (cy + 1) * height / SignatureHeight);
        for (int cx = 0; cx < SignatureWidth; ++cx) {
            const int x0 = cx * width / SignatureWidth, x1 = std::max(x0 + 1, (cx + 1) * width / SignatureWidth);
            uint64_t sum = 0, count = 0;
            for (int y = y0; y < y1 && y < height; ++y) {
                const uint8_t* row = bgra + static_cast<size_t>(y) * static_cast<size_t>(stride);
                for (int x = x0; x < x1 && x < width; ++x) {
                    const uint8_t* p = row + static_cast<size_t>(x) * 4;
                    sum += (static_cast<unsigned>(p[0]) + 2u * p[1] + p[2]) / 4u;
                    ++count;
                }
            }
            samples[static_cast<size_t>(cy) * SignatureWidth + cx] = static_cast<uint8_t>(count ? sum / count : 0);
        }
    }
    return samples;
}

bool ReadScheduler::changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const {
    return changed(a, b, changeThreshold_);
}

bool ReadScheduler::changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int threshold,
                            std::optional<TextBox> within) {
    if (a.size() != b.size()) return true;
    auto differs = [&](size_t i) { return std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > threshold; };
    constexpr int columns = SignatureWidth;
    const int rows = static_cast<int>(a.size()) / columns;
    if (!within || rows == 0 || a.size() % columns != 0) {
        for (size_t i = 0; i < a.size(); ++i)
            if (differs(i)) return true;
        return false;
    }
    const int left = std::clamp(static_cast<int>(std::floor(within->left * columns)), 0, columns - 1);
    const int right = std::clamp(static_cast<int>(std::ceil(within->right * columns)), left + 1, columns);
    const int top = std::clamp(static_cast<int>(std::floor(within->top * rows)), 0, rows - 1);
    const int bottom = std::clamp(static_cast<int>(std::ceil(within->bottom * rows)), top + 1, rows);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (differs(static_cast<size_t>(y * columns + x))) return true;
    return false;
}

}  // namespace crispplay::subs
