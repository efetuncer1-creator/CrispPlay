// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TermProtection.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include "crispplay/subs/Glossary.h"

#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

// Protects glossary terms from translation: a term becomes an invented proper name ("Zarmon",
// "Velkor") before translation; the model keeps names and adds the Turkish suffix after an
// apostrophe. Afterwards the name is found regardless of case, space and apostrophe, replaced
// by the Turkish term and the suffix is rebuilt for it (TurkishSuffix). A missing name makes the
// result unreliable (nullopt); the caller falls back to an unprotected translation.
class TermProtector {
public:
    static const std::vector<std::string>& placeholders();

    struct Slot {
        std::string placeholder, english, turkish;
        bool operator==(const Slot&) const = default;
    };
    struct Protected {
        std::string text;
        std::vector<Slot> slots;  // placeholders used in this text
    };

    // Placeholders occurring in any of the sources are not used.
    TermProtector(const Glossary& glossary, const std::vector<std::string>& sources);

    Protected protect(const std::string& text);
    // Every slot so far (context included), so that no unexpected name stays in the output.
    const std::vector<Slot>& allSlots() const { return all_; }

    // Placeholders -> Turkish terms. Each of `expected` must appear, other names of `all` must not.
    static std::optional<std::string> restore(const std::string& output, const std::vector<Slot>& expected,
                                              const std::vector<Slot>& all = {}, char32_t apostrophe = U'\'');

private:
    const Glossary& glossary_;
    std::vector<std::string> free_;
    std::vector<std::pair<std::u32string, Slot>> byTerm_;  // lower-case English -> slot
    std::vector<Slot> all_;
};

}  // namespace crispplay::subs
