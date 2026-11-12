// CrispPlay subtitle translation: types shared by the OCR stage, the translation engines and
// the overlay. Part of CrispPlay, GPL-3.0 (see LICENSE). Text is UTF-8 everywhere.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs {

// Pixels in the analysed band (x right, y down).
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(const Rect& o) const {
        return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
    }
    // Smallest rect holding both; an empty rect is ignored.
    Rect united(const Rect& o) const {
        if (o.empty()) return *this;
        if (empty()) return o;
        const float l = x < o.x ? x : o.x, t = y < o.y ? y : o.y;
        const float r = right() > o.right() ? right() : o.right(), b = bottom() > o.bottom() ? bottom() : o.bottom();
        return {l, t, r - l, b - t};
    }
};

// One detected line.
struct OcrLine {
    std::string text;
    Rect box;
    float confidence = -1.f;
};

struct OcrFrame {
    int64_t timeMs = 0;
    int width = 0, height = 0;  // band size in pixels
    std::vector<OcrLine> lines;
    // Where the band sits in the full video frame, as fractions of the frame (0-1). Used to
    // tell a centred subtitle from corner text and to widen a partly read centred line.
    float bandLeft = 0.f, bandTop = 0.f, bandRight = 1.f, bandBottom = 1.f;
};

// Implemented by the MT agent (CTranslate2) and later an online engine. One call = one
// English sentence (or a short line with its context, see LineTranslator).
class Translator {
public:
    virtual ~Translator() = default;
    // Blocking, called on a worker thread; nullopt = failure (model missing, error, cancelled).
    virtual std::optional<std::string> translate(const std::string& english) = 0;
    virtual void cancel() {}  // best effort; the next translate() call must work again
};

// What the overlay draws.
struct ShownLine {
    std::string speaker, text;  // speaker label without the colon ("MINE GUARD"); text never repeats it
    Rect cover;                 // band pixels: union of the subtitle's line boxes + margin
    bool original = false;      // translation unusable: `text` is the English line itself
    int64_t sinceMs = 0;        // pipeline clock when this line was shown
    // Same cover as fractions of the full video frame (bandLeft..bandBottom applied).
    float frameLeft = 0.f, frameTop = 0.f, frameRight = 0.f, frameBottom = 0.f;
};

}  // namespace crispplay::subs
