// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/Reading.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/Reading.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/SubtitleStabilizer.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <cmath>

namespace crispplay::subs {
namespace {

size_t wordCount(const std::string& text) { return uni::splitWhiteSpace(uni::decode(text)).size(); }

bool isBlank(const std::string& text) {
    const auto t = uni::decode(text);
    return std::all_of(t.begin(), t.end(), [](char32_t c) { return uni::isWhiteSpace(c); });
}

// A line right below the block's last line that overlaps it horizontally belongs to the block.
bool belongs(const TextBox& box, const std::vector<TextLine>& block, const TextBox& area) {
    const TextBox last = block.back().box->placedIn(area);
    const double height = std::max(box.bottom - box.top, last.bottom - last.top);
    if (box.top - last.bottom >= height * 0.8 || box.top < last.top) return false;
    const double overlap = std::min(box.right, last.right) - std::max(box.left, last.left);
    const double narrower = std::min(box.right - box.left, last.right - last.left);
    return overlap > 0 && overlap >= narrower * 0.5;
}

}  // namespace

TextBox TextBox::unite(const TextBox& o) const {
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom)};
}

TextBox TextBox::placedIn(const TextBox& area) const {
    const double width = area.right - area.left, height = area.bottom - area.top;
    return {area.left + left * width, area.top + top * height, area.left + right * width, area.top + bottom * height};
}

TextBox TextBox::centeredOnScreen() const {
    const double middle = (left + right) / 2;
    if (middle <= 0.3 || middle >= 0.7) return *this;
    TextBox out = *this;
    out.left = std::max(0.0, std::min(left, 1 - right));
    out.right = std::min(1.0, std::max(right, 1 - left));
    return out;
}

Reading::Reading(std::vector<TextLine> lines) {
    for (auto& line : lines)
        if (!isBlank(line.text)) lines_.push_back(std::move(line));
}

Reading Reading::fromFrame(const OcrFrame& frame) {
    std::vector<TextLine> lines;
    const double w = frame.width > 0 ? frame.width : 1, h = frame.height > 0 ? frame.height : 1;
    for (const auto& line : frame.lines) {
        TextLine out{line.text, std::nullopt, std::nullopt};
        if (!line.box.empty()) {
            out.box = TextBox{line.box.x / w, line.box.y / h, line.box.right() / w, line.box.bottom() / h};
            out.pixels = line.box;
        }
        lines.push_back(std::move(out));
    }
    return Reading(std::move(lines));
}

Reading Reading::fromText(const std::string& text) {
    std::vector<TextLine> lines;
    for (const auto& row : uni::split(uni::replaceAll(uni::decode(text), U"\r\n", U"\n"), U'\n'))
        lines.push_back({uni::encode(uni::trim(row)), std::nullopt, std::nullopt});
    return Reading(std::move(lines));
}

std::string Reading::text() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) out += '\n';
        out += lines_[i].text;
    }
    return out;
}

bool Reading::isSeparateText(double gap, double lineHeight, double imageWidth) {
    return gap > lineHeight * 4 && gap > imageWidth * 0.12;
}

std::optional<TextBox> Reading::box() const {
    std::optional<TextBox> total;
    for (const auto& line : lines_)
        if (line.box) total = total ? total->unite(*line.box) : *line.box;
    return total;
}

Rect Reading::pixelBox() const {
    Rect total;
    for (const auto& line : lines_)
        if (line.pixels) total = total.united(*line.pixels);
    return total;
}

Reading Reading::withoutButtonPrompts() const {
    std::vector<TextLine> kept;
    for (const auto& line : lines_)
        if (!ButtonPrompts::isPrompt(line.text)) kept.push_back(line);
    return Reading(std::move(kept));
}

Reading Reading::withoutOcrNoise() const {
    std::vector<TextLine> kept;
    for (auto line : lines_) {
        line.text = OcrNoise::clean(line.text);
        kept.push_back(std::move(line));
    }
    return Reading(std::move(kept));
}

Reading Reading::subtitleBlock(const TextBox& area) const {
    if (lines_.empty() || std::any_of(lines_.begin(), lines_.end(), [](const TextLine& l) { return !l.box; })) return *this;
    std::vector<TextLine> ordered = lines_;
    std::stable_sort(ordered.begin(), ordered.end(), [](const TextLine& a, const TextLine& b) { return a.box->top < b.box->top; });
    std::vector<std::vector<TextLine>> blocks;
    for (auto& line : ordered) {
        const TextBox placed = line.box->placedIn(area);
        int index = -1;
        for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i)
            if (belongs(placed, blocks[i], area)) { index = i; break; }
        if (index >= 0) blocks[index].push_back(std::move(line));
        else blocks.push_back({std::move(line)});
    }
    std::vector<Reading> candidates;
    for (auto& block : blocks) {
        Reading reading(std::move(block));
        if (!reading.isMenu()) candidates.push_back(std::move(reading));
    }
    // MaxBy: the first of the largest bottoms.
    const Reading* best = nullptr;
    for (const auto& reading : candidates) {
        const TextBox placed = reading.box()->placedIn(area);
        if (std::abs((placed.left + placed.right) / 2 - 0.5) > 0.12) continue;
        if (!best || reading.box()->bottom > best->box()->bottom) best = &reading;
    }
    if (best) return *best;
    for (const auto& reading : candidates) {
        if (wordCount(reading.text()) < 3) continue;
        if (!best || reading.box()->bottom > best->box()->bottom) best = &reading;
    }
    return best ? *best : Reading();
}

bool Reading::isMenu() const {
    return lines_.size() >= 3 && std::all_of(lines_.begin(), lines_.end(), [](const TextLine& line) {
               return wordCount(line.text) <= 3 && !SubtitleStabilizer::endsSentence(line.text);
           });
}

Reading Reading::placedIn(const TextBox& area) const {
    std::vector<TextLine> out = lines_;
    for (auto& line : out)
        if (line.box) line.box = line.box->placedIn(area);
    return Reading(std::move(out));
}

namespace ButtonPrompts {
namespace {

// Always controller buttons (OCR sometimes reads them with brackets). Compared ignoring case.
const std::u32string_view kButtons[] = {U"R1", U"R2", U"R3", U"L1", U"L2", U"L3", U"OPTIONS", U"TOUCHPAD",
                                        U"✕", U"×", U"⨯", U"✖", U"○", U"◯", U"●", U"□", U"▢", U"◻", U"■",
                                        U"△", U"▲", U"⨂"};
// A symbol can turn into a letter in OCR; a button only in the prompt pattern (ordinal).
const std::u32string_view kLookalikes[] = {U"X", U"O"};
const std::u32string_view kVerbs[] = {U"press", U"hold", U"tap", U"push", U"use", U"mash", U"release"};
const std::u32string_view kBrackets = U"()[]{}<>「」";

bool isButton(const std::u32string& t) {
    return std::any_of(std::begin(kButtons), std::end(kButtons), [&](auto b) { return uni::equalsIgnoreCase(t, b); });
}
bool isLookalike(const std::u32string& t) {
    return std::any_of(std::begin(kLookalikes), std::end(kLookalikes), [&](auto b) { return t == b; });
}
bool isVerb(const std::u32string& t) {
    return std::any_of(std::begin(kVerbs), std::end(kVerbs), [&](auto b) { return uni::equalsIgnoreCase(t, b); });
}

}  // namespace

bool isPrompt(const std::string& lineUtf8) {
    const auto line = uni::decode(lineUtf8);
    std::vector<std::u32string> tokens;
    std::u32string current;
    auto flush = [&] {
        size_t a = 0, b = current.size();
        while (a < b && uni::contains(kBrackets, current[a])) ++a;
        while (b > a && uni::contains(kBrackets, current[b - 1])) --b;
        if (b > a) tokens.push_back(current.substr(a, b - a));
        current.clear();
    };
    for (char32_t c : line) {
        if (c == ' ' || c == '\t' || c == '+' || c == '/') flush();
        else current.push_back(c);
    }
    flush();
    if (tokens.empty()) return false;
    const auto& first = tokens[0];
    const auto words = std::count_if(tokens.begin(), tokens.end(), [](const std::u32string& t) {
        return !isButton(t) && uni::anyLetter(t);
    });
    // "R2 Swing", "△ Pick up", "L1 R1 Spartan Rage"
    if (isButton(first)) return words <= 5;
    // "PHOTO MODE L1 + R1": button last, a short unpunctuated action name before it.
    if (isButton(tokens.back()) && words <= 4 && !SubtitleStabilizer::endsSentence(lineUtf8)) return true;
    // "X Interact", "O Back": a short capitalised unpunctuated action follows.
    if (isLookalike(first) && tokens.size() >= 2 && tokens.size() <= 4 && uni::isUpper(tokens[1][0]) &&
        !SubtitleStabilizer::endsSentence(lineUtf8))
        return true;
    // "Press ✕ to jump", "Hold R2 to aim"
    if (isVerb(first) && tokens.size() >= 2 && tokens.size() <= 10) return isButton(tokens[1]) || isLookalike(tokens[1]);
    return false;
}

}  // namespace ButtonPrompts
}  // namespace crispplay::subs
