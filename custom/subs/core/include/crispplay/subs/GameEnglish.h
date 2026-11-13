// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/GameEnglish.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <string>

namespace crispplay::subs::GameEnglish {

// Game phrases the on-device model translates word by word ("I've got your six") become plain
// English with the same meaning before translation; a leading capital is kept.
std::string plain(const std::string& sentence);

}  // namespace crispplay::subs::GameEnglish
