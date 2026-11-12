#pragma once
// Shared subtitle types. The canonical definition lives in the text-pipeline
// library (custom/subs/core/include/crispplay/subs/Types.h); until that is
// merged this header carries an identical local copy. Once core's include
// directory is visible, its header wins and the copy below is skipped.
#if __has_include(<crispplay/subs/Types.h>)
#include <crispplay/subs/Types.h>
#else
#include <cstdint>
#include <string>
#include <vector>

namespace crispplay::subs {
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
struct OcrLine { std::string text; Rect box; float confidence = -1.f; };
struct OcrFrame { int64_t timeMs = 0; int width = 0, height = 0; std::vector<OcrLine> lines; };
}
#endif
