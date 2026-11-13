// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/ReadScheduler.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/ReadScheduler.h"

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

bool ReadScheduler::changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const {
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > changeThreshold_) return true;
    return false;
}

}  // namespace crispplay::subs
