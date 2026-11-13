// Draws the translated line as a picture for upstream's layered subtitle
// window: a dark box that covers the whole English subtitle, the speaker name
// in a highlight colour and the Turkish text. The window takes pictures as
// palette + run-length frames (the path PGS subtitles use), placed in video
// coordinates, so the box lands exactly on the English line for every source
// and window size. UI thread (Qt text rendering).
#pragma once

#include <memory>

#include <QString>

#include "crispplay/subs/Types.h"
#include "veyra/engine/Subtitles.h"

namespace crispplay::subs::app {

struct OverlayStyle {
    double scale = 1.0;          // text size multiplier
    int opacity = 92;            // box opacity, percent
    quint32 textColor = 0xFFFFFFFFu, speakerColor = 0xFFFFD54Au, boxColor = 0xFF0B0B0Du;   // ARGB
    QString family = QStringLiteral("Segoe UI");
};

// Text that can be drawn: characters no installed font covers are removed
// (never a box), the unknown-token mark U+2047 and control characters too.
QString displayable(const QString& text, const QString& family);

// `canvasWidth` x `canvasHeight`: the picture's size on screen in pixels.
// Returns null when there is nothing to draw.
std::shared_ptr<const veyra::engine::SubtitleBitmapFrame> renderLine(const ShownLine& line, int canvasWidth, int canvasHeight,
                                                                     const OverlayStyle& style);

// For tests: the box of the last renderLine call in canvas pixels (x, y, w, h).
struct RenderedBox { int x = 0, y = 0, width = 0, height = 0; };
RenderedBox lastRenderedBox();

} // namespace crispplay::subs::app
