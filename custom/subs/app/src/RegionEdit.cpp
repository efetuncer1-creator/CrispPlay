#include "RegionEdit.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace crispplay::subs::app {
namespace {

double finiteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }
double clamp100(double v) { return std::clamp(v, 0.0, 100.0); }

// One axis: [a, b] ordered, inside 0..100, at least `minimum` long.
void fitAxis(double& a, double& b, double minimum) {
    a = clamp100(a); b = clamp100(b);
    if (b < a) std::swap(a, b);
    if (b - a < minimum) {
        const double grow = minimum - (b - a);
        a -= grow / 2; b += grow / 2;
        if (a < 0) { b -= a; a = 0; }
        if (b > 100) { a -= b - 100; b = 100; }
    }
}

const RegionPreset kPresets[] = {
    {"default", "Alt bant (varsayılan)", {14, 62, 86, 97}},
    {"wide", "Alt geniş", {5, 58, 95, 98}},
    {"low", "Alt kenar", {14, 76, 86, 98}},
    {"top", "Üst bant", {14, 3, 86, 30}},
    {"half", "Alt yarı", {0, 50, 100, 100}},
    {"full", "Tüm ekran", {0, 0, 100, 100}},
};

} // namespace

RegionPct clampRegion(RegionPct r) {
    r.left = finiteOr(r.left, 14); r.right = finiteOr(r.right, 86);
    r.top = finiteOr(r.top, 62); r.bottom = finiteOr(r.bottom, 97);
    fitAxis(r.left, r.right, kRegionMinWidthPct);
    fitAxis(r.top, r.bottom, kRegionMinHeightPct);
    return r;
}

RegionPct regionFromDrag(double x0, double y0, double x1, double y1) {
    x0 = clamp100(finiteOr(x0, 0)); y0 = clamp100(finiteOr(y0, 0));
    x1 = clamp100(finiteOr(x1, x0)); y1 = clamp100(finiteOr(y1, y0));
    // Grow a too small drag away from the start point, in the drag's direction.
    auto axis = [](double start, double end, double minimum, double& lo, double& hi) {
        lo = std::min(start, end); hi = std::max(start, end);
        if (hi - lo >= minimum) return;
        if (end >= start) { lo = start; hi = start + minimum; if (hi > 100) { hi = 100; lo = 100 - minimum; } }
        else { hi = start; lo = start - minimum; if (lo < 0) { lo = 0; hi = minimum; } }
    };
    RegionPct r;
    axis(x0, x1, kRegionMinWidthPct, r.left, r.right);
    axis(y0, y1, kRegionMinHeightPct, r.top, r.bottom);
    return r;
}

RegionPct moveRegion(const RegionPct& region, double dx, double dy) {
    RegionPct r = clampRegion(region);
    dx = std::clamp(finiteOr(dx, 0), -r.left, 100 - r.right);
    dy = std::clamp(finiteOr(dy, 0), -r.top, 100 - r.bottom);
    r.left += dx; r.right += dx; r.top += dy; r.bottom += dy;
    return r;
}

RegionPct resizeRegion(const RegionPct& region, int edges, double dx, double dy) {
    RegionPct r = clampRegion(region);
    dx = finiteOr(dx, 0); dy = finiteOr(dy, 0);
    if (edges & EdgeLeft) r.left = std::clamp(r.left + dx, 0.0, r.right - kRegionMinWidthPct);
    else if (edges & EdgeRight) r.right = std::clamp(r.right + dx, r.left + kRegionMinWidthPct, 100.0);
    if (edges & EdgeTop) r.top = std::clamp(r.top + dy, 0.0, r.bottom - kRegionMinHeightPct);
    else if (edges & EdgeBottom) r.bottom = std::clamp(r.bottom + dy, r.top + kRegionMinHeightPct, 100.0);
    return r;
}

RegionInt roundRegion(const RegionPct& region) {
    const RegionPct r = clampRegion(region);
    RegionInt out{int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom))};
    // Rounding can take a minimum-size region just under the minimum.
    auto fix = [](int& a, int& b, int minimum) {
        if (b - a >= minimum) return;
        b = a + minimum;
        if (b > 100) { b = 100; a = 100 - minimum; }
    };
    fix(out.left, out.right, int(kRegionMinWidthPct));
    fix(out.top, out.bottom, int(kRegionMinHeightPct));
    return out;
}

std::span<const RegionPreset> regionPresets() { return kPresets; }

const RegionPreset* findRegionPreset(const char* id) {
    if (!id) return nullptr;
    for (const auto& preset : kPresets) if (std::strcmp(preset.id, id) == 0) return &preset;
    return nullptr;
}

const RegionPreset* presetOf(const RegionInt& region) {
    for (const auto& preset : kPresets) if (preset.region == region) return &preset;
    return nullptr;
}

} // namespace crispplay::subs::app
