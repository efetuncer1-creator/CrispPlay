// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TranslationCache.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace crispplay::subs {

// Persistent cache of translated lines and sentences. Each engine and language pair has its own
// file. The file is an append-only log, one JSON array ["source","translation"] per line; at
// most `limit` entries stay in memory (least recently used dropped); when the file grows to
// twice the limit it is rewritten. Disk errors never stop translation: the cache then works in
// memory only. Thread-safe.
class TranslationCache {
public:
    static constexpr int DefaultLimit = 20000;
    // Format version in the file name.
    static constexpr int FormatVersion = 1;

    // No file: memory only.
    explicit TranslationCache(std::optional<std::filesystem::path> file = std::nullopt, int limit = DefaultLimit);

    // <directory>/v1-<engine>-<source>-<target>.jsonl (non letters/digits become '_').
    static std::filesystem::path fileFor(const std::filesystem::path& directory, const std::string& engine,
                                         const std::string& source, const std::string& target);
    // One cache per file for the whole process.
    static std::shared_ptr<TranslationCache> shared(const std::filesystem::path& directory, const std::string& engine,
                                                    const std::string& source, const std::string& target);

    size_t count();
    std::optional<std::string> get(const std::string& key);
    void store(const std::string& key, const std::string& value);

private:
    struct Entry { std::string value; int64_t used; };
    void loadIfNeeded();
    void evictIfNeeded();
    void append(const std::string& key, const std::string& value);
    void rewrite();

    std::optional<std::filesystem::path> file_;
    size_t limit_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    int64_t tick_ = 0;
    bool loaded_ = false;
    size_t fileLines_ = 0;
};

}  // namespace crispplay::subs
