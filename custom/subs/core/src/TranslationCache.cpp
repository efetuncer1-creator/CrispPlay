// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/TranslationCache.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
//
// Port note: lines are written with UTF-8 text unescaped (System.Text.Json escapes non-ASCII);
// both forms are read.
#include "crispplay/subs/TranslationCache.h"

#include "crispplay/subs/Json.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <vector>

namespace crispplay::subs {
namespace {

std::string line(const std::string& key, const std::string& value) {
    return "[" + json::quote(key) + "," + json::quote(value) + "]\n";
}

std::string safe(const std::string& part) {
    auto text = uni::decode(part);
    for (auto& c : text)
        if (!(uni::isLetterOrDigit(c) || c == U'_')) c = U'_';
    return uni::encode(text);
}

}  // namespace

TranslationCache::TranslationCache(std::optional<std::filesystem::path> file, int limit)
    : file_(std::move(file)), limit_(static_cast<size_t>(std::max(1, limit))) {}

std::filesystem::path TranslationCache::fileFor(const std::filesystem::path& directory, const std::string& engine,
                                                const std::string& source, const std::string& target) {
    const std::string name = safe("v" + std::to_string(FormatVersion)) + "-" + safe(engine) + "-" + safe(source) + "-" +
                             safe(target) + ".jsonl";
    return directory / std::filesystem::path(std::u8string(name.begin(), name.end()));
}

std::shared_ptr<TranslationCache> TranslationCache::shared(const std::filesystem::path& directory, const std::string& engine,
                                                           const std::string& source, const std::string& target) {
    static std::mutex mutex;
    static std::map<std::u32string, std::shared_ptr<TranslationCache>> opened;
    const auto file = fileFor(directory, engine, source, target);
    const auto key = uni::toLowerInvariant(std::filesystem::absolute(file).lexically_normal().u32string());
    std::lock_guard lock(mutex);
    auto& slot = opened[key];
    if (!slot) slot = std::make_shared<TranslationCache>(file);
    return slot;
}

size_t TranslationCache::count() {
    std::lock_guard lock(mutex_);
    loadIfNeeded();
    return entries_.size();
}

std::optional<std::string> TranslationCache::get(const std::string& key) {
    std::lock_guard lock(mutex_);
    loadIfNeeded();
    auto it = entries_.find(key);
    if (it == entries_.end()) return std::nullopt;
    it->second.used = ++tick_;
    return it->second.value;
}

void TranslationCache::store(const std::string& key, const std::string& value) {
    if (key.empty() || value.empty()) return;
    std::lock_guard lock(mutex_);
    loadIfNeeded();
    auto it = entries_.find(key);
    const bool same = it != entries_.end() && it->second.value == value;
    entries_[key] = Entry{value, ++tick_};
    if (same) return;
    evictIfNeeded();
    append(key, value);
}

void TranslationCache::loadIfNeeded() {
    if (loaded_) return;
    loaded_ = true;
    if (!file_) return;
    std::ifstream in(*file_, std::ios::binary);
    if (!in) return;
    std::string text;
    bool firstLine = true;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (firstLine && text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
        firstLine = false;
        if (text.empty()) continue;
        ++fileLines_;
        const auto pair = json::parse(text);
        if (!pair || !pair->isArray() || pair->array.size() != 2 || !pair->array[0].isString() || !pair->array[1].isString())
            continue;
        if (pair->array[0].string.empty() || pair->array[1].string.empty()) continue;
        entries_[pair->array[0].string] = Entry{pair->array[1].string, ++tick_};
    }
    in.close();
    evictIfNeeded();
    if (fileLines_ > entries_.size() + limit_ / 2) rewrite();
}

void TranslationCache::evictIfNeeded() {
    if (entries_.size() <= limit_) return;
    // A tenth goes at once so that sorting is rare.
    const size_t drop = entries_.size() - limit_ + limit_ / 10;
    std::vector<std::pair<int64_t, std::string>> byUse;
    byUse.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) byUse.emplace_back(entry.used, key);
    std::sort(byUse.begin(), byUse.end());
    for (size_t i = 0; i < drop && i < byUse.size(); ++i) entries_.erase(byUse[i].second);
}

void TranslationCache::append(const std::string& key, const std::string& value) {
    if (!file_) return;
    if (fileLines_ >= limit_ * 2) {
        rewrite();
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(file_->parent_path(), ec);
    std::ofstream out(*file_, std::ios::binary | std::ios::app);
    if (!out) return;  // disk full or read-only: memory only
    out << line(key, value);
    if (out) ++fileLines_;
}

void TranslationCache::rewrite() {
    if (!file_) return;
    std::vector<std::pair<int64_t, const std::string*>> byUse;
    for (const auto& [key, entry] : entries_) byUse.emplace_back(entry.used, &key);
    std::sort(byUse.begin(), byUse.end());
    std::string content;
    for (const auto& [used, key] : byUse) content += line(*key, entries_.at(*key).value);
    std::error_code ec;
    std::filesystem::create_directories(file_->parent_path(), ec);
    auto temporary = *file_;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << content;
        if (!out) return;
    }
    std::filesystem::rename(temporary, *file_, ec);
    if (!ec) fileLines_ = entries_.size();
}

}  // namespace crispplay::subs
