// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/Glossary.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace crispplay::subs {

// Term glossary of the offline translation. File format:
// { "game": "God of War", "terms": { "Leviathan Axe": "Leviathan Baltası" }, "keep": ["Kratos"] }
// `terms` replaces the English term with the given Turkish, `keep` leaves names as they are.
// Matching ignores case, only whole words, longer terms first. The built-in general glossary
// is merged with the user's *.json files (name order; a later file wins for the same term).
class Glossary {
public:
    struct TermMatch {
        size_t start = 0, length = 0;  // code points in the searched text
        std::u32string english;        // as written in the glossary
        std::u32string turkish;
    };

    Glossary() : Glossary(Terms{}) {}

    static const Glossary& builtIn();
    // nullopt if the text is not a JSON object.
    static std::optional<Glossary> parse(const std::string& json);
    static Glossary merge(const std::vector<const Glossary*>& glossaries);
    // Built-in glossary plus the *.json files of `directory`; unreadable or broken files are skipped.
    static Glossary load(const std::optional<std::filesystem::path>& directory);
    static std::vector<std::filesystem::path> userFiles(const std::optional<std::filesystem::path>& directory);

    size_t count() const { return terms_.size(); }
    bool empty() const { return terms_.empty(); }
    // Words to keep capitalised (lower case); handed to normalizeCase as names.
    const std::set<std::string>& capitalWords() const { return capitalWords_; }
    // Short trace of the content; part of cache keys of lines with terms.
    const std::string& fingerprint() const { return fingerprint_; }
    std::optional<std::string> turkish(const std::string& english) const;

    std::vector<TermMatch> find(std::u32string_view text) const;
    bool matches(const std::string& text) const;

private:
    // lower-case English -> (English as written, Turkish), UTF-32.
    using Terms = std::map<std::u32string, std::pair<std::u32string, std::u32string>>;
    explicit Glossary(Terms terms);
    static void add(Terms& terms, std::u32string english, std::u32string turkish);

    Terms terms_;
    std::vector<std::pair<std::u32string, std::u32string>> ordered_;  // longest first
    std::set<std::string> capitalWords_;
    std::string fingerprint_;
};

// Watches the user glossary folder; reloads when a file changes (name, size, time), looking
// at most every few seconds.
class GlossaryStore {
public:
    static constexpr int64_t CheckEveryMs = 5000;
    using Clock = std::function<int64_t()>;  // milliseconds

    explicit GlossaryStore(std::optional<std::filesystem::path> directory, Clock clock = {});
    static GlossaryStore builtInOnly() { return GlossaryStore(std::nullopt); }

    std::shared_ptr<const Glossary> current();

private:
    std::string stamp() const;

    std::optional<std::filesystem::path> directory_;
    Clock clock_;
    std::mutex mutex_;
    std::shared_ptr<const Glossary> current_;
    std::string stamp_;
    int64_t checkedAt_ = 0;
};

}  // namespace crispplay::subs
