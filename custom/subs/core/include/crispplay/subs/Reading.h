// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/Reading.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#pragma once

#include "crispplay/subs/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

// Place of text as fractions of the area it was read in (0-1, from top and left).
struct TextBox {
    double left = 0, top = 0, right = 0, bottom = 0;

    TextBox unite(const TextBox& o) const;
    // This box is relative to `area`; the same place relative to the frame holding `area`.
    TextBox placedIn(const TextBox& area) const;
    // A centred subtitle's box is widened symmetrically around the screen centre, so a line
    // read only in part is still covered; corner text is left alone.
    TextBox centeredOnScreen() const;
    bool operator==(const TextBox&) const = default;
};

// One line found by OCR; box relative to the read area, if known. `pixels` keeps the original
// OCR box (band pixels) so the overlay cover can be computed from geometry.
struct TextLine {
    std::string text;
    std::optional<TextBox> box;
    std::optional<Rect> pixels;
};

// The lines of one frame, top to bottom.
class Reading {
public:
    Reading() = default;
    explicit Reading(std::vector<TextLine> lines);  // white-space-only lines are dropped

    // From an OcrFrame: boxes become fractions of the band.
    static Reading fromFrame(const OcrFrame& frame);
    // Each line break is a line (readers without line boxes).
    static Reading fromText(const std::string& text);

    const std::vector<TextLine>& lines() const { return lines_; }
    bool empty() const { return lines_.empty(); }
    std::string text() const;  // lines joined with '\n'

    // Is a gap (pixels) between two words of one OCR line a gap between separate texts?
    // Only when wider than 4 line heights and 12 % of the image width.
    static bool isSeparateText(double gap, double lineHeight, double imageWidth);

    // Union of the line boxes; nullopt if no line has one.
    std::optional<TextBox> box() const;
    // Union of the lines' pixel boxes (empty rect if none).
    Rect pixelBox() const;

    Reading withoutButtonPrompts() const;
    Reading withoutOcrNoise() const;
    // The subtitle block of the area: lines grouped by place; the lowest centred non-menu
    // block, else the lowest non-menu block of 3+ words. `area` is the read area's place in
    // the frame. Lines without boxes: the reading stays as is.
    Reading subtitleBlock(const TextBox& area) const;
    // Short unpunctuated lines stacked 3+ ("MAP", "HELP", "SOCIAL CLUB") are a menu.
    bool isMenu() const;
    Reading placedIn(const TextBox& area) const;

private:
    std::vector<TextLine> lines_;
};

// Controller prompts shown by games: "R2 Swing", "L1 + R1 Spartan Rage", "△ Pick up",
// "Press ✕ to jump". Speech lines are left alone.
namespace ButtonPrompts {
bool isPrompt(const std::string& line);
}

}  // namespace crispplay::subs
