// Geometry of the region picker (custom/qml/SubsRegionPicker.qml): drawing,
// moving and resizing the read region, and its presets. Percent of the frame
// (0..100), the unit the settings file and the settings tab use. No Qt, no
// D3D: unit tested (tests/AppTests.cpp).
#pragma once

#include <span>

#include "BandFormat.h"

namespace crispplay::subs::app {

struct RegionPct {
    double left = 14, top = 62, right = 86, bottom = 97;
    bool operator==(const RegionPct&) const = default;
};

// Same minimum as sanitized(): 10 % of the width, 5 % of the height.
inline constexpr double kRegionMinWidthPct = 10.0, kRegionMinHeightPct = 5.0;

// Ordered, inside 0..100, at least the minimum size (grown towards the inside).
RegionPct clampRegion(RegionPct region);
// A rectangle drawn by dragging from (x0, y0) to (x1, y1), percent of the
// frame. Points outside the frame are pulled in; a drag smaller than the
// minimum grows from the start point in the drag's direction.
RegionPct regionFromDrag(double x0, double y0, double x1, double y1);
// The whole rectangle shifted, stopped at the frame's edges (size kept).
RegionPct moveRegion(const RegionPct& region, double dx, double dy);

// Edges a resize handle moves (corner handles move two).
enum RegionEdge : int { EdgeLeft = 1, EdgeTop = 2, EdgeRight = 4, EdgeBottom = 8 };
// Moves the given edges; an edge stops at the frame and at the minimum size
// (it never passes the opposite edge).
RegionPct resizeRegion(const RegionPct& region, int edges, double dx, double dy);

// Whole percent, as stored; still a valid region after rounding.
struct RegionInt { int left = 14, top = 62, right = 86, bottom = 97; bool operator==(const RegionInt&) const = default; };
RegionInt roundRegion(const RegionPct& region);

struct RegionPreset {
    const char* id;
    const char* label;      // Turkish, shown in the picker and the menu
    RegionInt region;
};
// default (Alt bant), wide, low, top (Üst bant), half (Alt yarı), full (Tüm ekran).
std::span<const RegionPreset> regionPresets();
const RegionPreset* findRegionPreset(const char* id);
// The preset whose numbers these are, or nullptr (custom).
const RegionPreset* presetOf(const RegionInt& region);

inline Region toFraction(const RegionInt& r) {
    return Region{float(r.left) / 100.0f, float(r.top) / 100.0f, float(r.right) / 100.0f, float(r.bottom) / 100.0f};
}

} // namespace crispplay::subs::app
