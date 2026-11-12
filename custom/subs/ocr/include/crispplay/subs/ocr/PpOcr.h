#pragma once
// Second reader: the PP-OCR text recognition network (PP-OCRv6 small rec,
// Apache-2.0) on ONNX Runtime (MIT), CPU only. It reads one line crop; finding
// the lines is Windows OCR's job (the PP-OCR detector costs ~400 ms).
//
// ONNX Runtime is loaded at run time from `runtimeDir` (default: the folder of
// the running executable) with LoadLibraryEx, so a missing or wrong DLL never
// stops the app from starting: create() fails and the caller stays Windows-only.
// Built without ONNX Runtime headers (CRISPPLAY_SUBS_HAVE_ORT=0) create() always fails.
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "crispplay/subs/ocr/BandImage.h"

namespace crispplay::subs::ocr {

struct PpOcrOptions {
    std::filesystem::path modelPath;   // PP-OCRv6_rec_small.onnx
    std::filesystem::path dictPath;    // optional; default: the "character" list in the model metadata
    std::filesystem::path runtimeDir;  // folder holding onnxruntime.dll; empty = executable's folder
    int threads = 2;                   // intra-op threads (spike: 2)
};

struct PpOcrReading {
    std::string text;         // UTF-8
    float confidence = 0.f;   // mean max-probability of the kept CTC steps
};

class PpOcrRecognizer {
public:
    static std::unique_ptr<PpOcrRecognizer> create(const PpOcrOptions& options, std::string* error = nullptr);
    ~PpOcrRecognizer();

    // `line` is a crop holding one text line. Thread-safe.
    PpOcrReading recognize(const BandImage& line);

    int inputHeight() const;            // 48 for PP-OCRv6
    size_t dictionarySize() const;
    static bool runtimeCompiledIn();    // CRISPPLAY_SUBS_HAVE_ORT

private:
    PpOcrRecognizer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// CTC greedy decode of a [steps x classes] probability matrix: argmax per step,
// repeats collapsed, class 0 = blank, 1..N = dict, N+1 = space. Exposed for tests.
PpOcrReading ctcGreedyDecode(const float* probs, int steps, int classes, const std::vector<std::string>& dict);

// RapidOCR 3.10 / PaddleOCR rec preprocessing for one image: the RapidOCR size
// guard (sides kept within 30..2000, multiples of 32), resize to `height` keeping
// the aspect, BGR planes scaled to [-1, 1], right-padded with 0 to at least
// height * 320 / 48. Returns CHW floats, width in `*width`. Exposed for tests.
std::vector<float> ppocrInput(const BandImage& line, int height, int* width);

}  // namespace crispplay::subs::ocr
