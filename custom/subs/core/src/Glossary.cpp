// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/Glossary.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/Glossary.h"

#include "crispplay/subs/Json.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace crispplay::subs {
namespace {

#include "BuiltInGlossary.inc"

std::u32string collapseSpaces(std::u32string_view text) { return uni::join(uni::splitWhiteSpace(text), U" "); }

bool isBoundary(std::u32string_view text, size_t index, bool before) {
    if (before && index == 0) return true;
    const size_t i = before ? index - 1 : index;
    return i >= text.size() || !uni::isLetterOrDigit(text[i]);
}

int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::optional<std::string> readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return std::nullopt;
    return buffer.str();
}

}  // namespace

Glossary::Glossary(Terms terms) : terms_(std::move(terms)) {
    for (const auto& [key, value] : terms_) ordered_.push_back(value);
    std::sort(ordered_.begin(), ordered_.end(), [](const auto& a, const auto& b) {
        if (a.first.size() != b.first.size()) return a.first.size() > b.first.size();
        return a.first < b.first;
    });
    for (const auto& [english, turkish] : ordered_) {
        // Terms written with a capital are names: they stay capitalised in an all-caps line.
        if (!std::any_of(english.begin(), english.end(), [](char32_t c) { return uni::isUpper(c); })) continue;
        for (const auto& word : uni::split(english, U' '))
            if (word.size() >= 2) capitalWords_.insert(uni::encode(uni::toLowerInvariant(word)));
    }
    // FNV-1a (32 bit) over the terms sorted by lower-case English (same as CouchSubs).
    uint32_t hash = 2166136261u;
    for (const auto& [key, value] : terms_) {
        const std::string bytes = uni::encode(key) + "\x01" + uni::encode(value.second) + "\x02";
        for (unsigned char b : bytes) hash = (hash ^ b) * 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "g%08x", hash);
    fingerprint_ = buf;
}

const Glossary& Glossary::builtIn() {
    static const Glossary value = parse(kBuiltInGlossaryJson).value_or(Glossary());
    return value;
}

void Glossary::add(Terms& terms, std::u32string english, std::u32string turkish) {
    english = collapseSpaces(english);
    turkish = collapseSpaces(turkish);
    // A term without letters would match everywhere; placeholders are few, so no long terms.
    if (!uni::anyLetter(english) || turkish.empty() || english.size() > 80) return;
    terms[uni::toLowerInvariant(english)] = {english, turkish};
}

std::optional<Glossary> Glossary::parse(const std::string& text) {
    const auto root = json::parse(text, true);
    if (!root || !root->isObject()) return std::nullopt;
    Terms terms;
    for (const auto& [name, member] : root->object) {
        if (name == "terms" && member.isObject()) {
            for (const auto& [english, turkish] : member.object)
                if (turkish.isString()) add(terms, uni::decode(english), uni::decode(turkish.string));
        }
    }
    for (const auto& [name, member] : root->object) {
        if (name == "keep" && member.isArray()) {
            for (const auto& item : member.array)
                if (item.isString()) add(terms, uni::decode(item.string), uni::decode(item.string));
        }
    }
    return Glossary(std::move(terms));
}

Glossary Glossary::merge(const std::vector<const Glossary*>& glossaries) {
    Terms terms;
    for (const auto* glossary : glossaries)
        for (const auto& [key, value] : glossary->terms_) terms[key] = value;
    return Glossary(std::move(terms));
}

std::vector<std::filesystem::path> Glossary::userFiles(const std::optional<std::filesystem::path>& directory) {
    std::vector<std::filesystem::path> files;
    if (!directory) return files;
    std::error_code ec;
    if (!std::filesystem::is_directory(*directory, ec)) return files;
    for (std::filesystem::directory_iterator it(*directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto ext = it->path().extension().u32string();
        if (uni::equalsIgnoreCase(ext, U".json")) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.u32string() < b.u32string(); });
    return files;
}

Glossary Glossary::load(const std::optional<std::filesystem::path>& directory) {
    std::vector<Glossary> user;
    for (const auto& file : userFiles(directory))
        if (auto content = readFile(file))
            if (auto parsed = parse(*content)) user.push_back(std::move(*parsed));
    std::vector<const Glossary*> all{&builtIn()};
    for (const auto& g : user) all.push_back(&g);
    return merge(all);
}

std::optional<std::string> Glossary::turkish(const std::string& english) const {
    auto it = terms_.find(uni::toLowerInvariant(collapseSpaces(uni::decode(english))));
    if (it == terms_.end()) return std::nullopt;
    return uni::encode(it->second.second);
}

std::vector<Glossary::TermMatch> Glossary::find(std::u32string_view text) const {
    std::vector<TermMatch> found;
    if (ordered_.empty() || text.empty()) return found;
    for (const auto& [english, turkish] : ordered_) {
        size_t start = 0;
        while (start + english.size() <= text.size()) {
            const size_t index = uni::findIgnoreCase(text, english, start);
            if (index == std::u32string::npos) break;
            const size_t end = index + english.size();
            const bool overlaps = std::any_of(found.begin(), found.end(), [&](const TermMatch& m) {
                return index < m.start + m.length && m.start < end;
            });
            if (isBoundary(text, index, true) && isBoundary(text, end, false) && !overlaps)
                found.push_back({index, english.size(), english, turkish});
            start = index + 1;
        }
    }
    std::sort(found.begin(), found.end(), [](const TermMatch& a, const TermMatch& b) { return a.start < b.start; });
    return found;
}

bool Glossary::matches(const std::string& text) const { return !find(uni::decode(text)).empty(); }

GlossaryStore::GlossaryStore(std::optional<std::filesystem::path> directory, Clock clock)
    : directory_(std::move(directory)), clock_(clock ? std::move(clock) : Clock(steadyMs)) {}

std::shared_ptr<const Glossary> GlossaryStore::current() {
    std::lock_guard lock(mutex_);
    const int64_t now = clock_();
    if (current_ && now - checkedAt_ < CheckEveryMs) return current_;
    checkedAt_ = now;
    auto s = stamp();
    if (!current_ || s != stamp_) {
        current_ = std::make_shared<const Glossary>(Glossary::load(directory_));
        stamp_ = std::move(s);
    }
    return current_;
}

std::string GlossaryStore::stamp() const {
    std::string out;
    for (const auto& file : Glossary::userFiles(directory_)) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec) continue;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec) continue;
        const auto name = file.u8string();
        out += std::string(name.begin(), name.end()) + "|" + std::to_string(size) + "|" + std::to_string(time.time_since_epoch().count()) + "\n";
    }
    return out;
}

}  // namespace crispplay::subs
