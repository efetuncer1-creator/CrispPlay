// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TurkishPostEdit.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs::TurkishPostEdit {

// Safe fixes of the on-device Turkish: no space before punctuation, the source's ! ? ?! …
// ending kept, quotes straight or curly like the source.
std::string apply(const std::string& source, const std::string& translation);
std::string ending(const std::string& source, const std::string& translation);
std::string quotes(const std::string& source, const std::string& translation);
// Curly apostrophe if the source uses one.
char32_t apostrophe(const std::string& source);
// If any sentence of a subtitle uses "sen", known "siz" imperatives of sentences without "siz"
// become "sen" imperatives. Nothing else is touched.
std::vector<std::optional<std::string>> registerForm(const std::vector<std::optional<std::string>>& translations);

}  // namespace crispplay::subs::TurkishPostEdit
