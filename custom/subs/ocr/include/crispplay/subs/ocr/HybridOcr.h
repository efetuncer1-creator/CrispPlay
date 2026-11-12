#pragma once
// Hybrid reader (spike: custom/subs/spike/real_scan.py + real_report.py "combo+fix"):
// Windows OCR finds and reads the lines; every text row is cut out again (centred
// rows widened symmetrically, since Windows OCR sometimes drops the first or last
// word) and read by the PP-OCR recognition network; per row the more word-like
// of the two readings is kept. Without the model it is Windows OCR only.
// "White text only" preprocessing runs only when the band reads empty.
//
// Same threading rule as WindowsOcrReader: construct and read on a worker thread.
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "crispplay/subs/ocr/BandImage.h"
#include "crispplay/subs/ocr/OcrTypes.h"
#include "crispplay/subs/ocr/WindowsOcr.h"

namespace crispplay::subs::ocr {

struct HybridOptions {
    std::string language = "en-US";
    std::filesystem::path ppocrModel;  // empty or missing: Windows OCR only
    std::filesystem::path runtimeDir;  // onnxruntime.dll folder; empty = executable's folder
    int ppocrThreads = 2;
    bool repairText = true;            // spike's three repairs before scoring (and in the output)
    bool whiteTextFallback = true;
};

struct HybridLine {
    std::string windowsText;   // Windows OCR words of this row, left to right
    std::string ppocrText;     // empty when PP-OCR did not run
    float ppocrConfidence = -1.f;
    Rect windowsBox;           // union of the Windows OCR line boxes in the row
    Rect cropBox;              // what PP-OCR read (padded, centred rows symmetric)
    bool ppocrChosen = false;
};

struct HybridDetail {
    std::vector<WinOcrLine> rawLines;    // Windows OCR on the band as is
    std::vector<WinOcrLine> whiteLines;  // only when the white-text fallback ran
    bool usedWhiteFallback = false;
    std::vector<HybridLine> rows;
    double windowsMs = 0;  // copy + recognise, both readings
    double ppocrMs = 0;
    double totalMs = 0;
    std::string error;
};

struct ReadingChoice {
    std::string text;
    bool ppocr = false;
};
// Keeps the more word-like reading; a tie goes to PP-OCR (spike rule).
ReadingChoice chooseReading(std::string_view windowsText, std::string_view ppocrText, bool havePpocr, bool repair);

// Windows OCR lines grouped into text rows (a row's mid height inside the previous row).
struct TextRow {
    Rect box;
    std::string text;
};
std::vector<TextRow> mergeRows(const std::vector<WinOcrLine>& lines);

// The crop PP-OCR reads for a row box in a band `bandW` x `bandH`.
Rect ppocrCrop(const Rect& row, int bandW, int bandH);

class PpOcrRecognizer;

class HybridOcrReader {
public:
    explicit HybridOcrReader(HybridOptions options = {});
    ~HybridOcrReader();
    HybridOcrReader(const HybridOcrReader&) = delete;
    HybridOcrReader& operator=(const HybridOcrReader&) = delete;

    bool windowsReady() const;
    bool ppocrReady() const;
    const WindowsOcrStatus& windowsStatus() const;
    const std::string& ppocrStatus() const;  // why PP-OCR is off; empty when on

    // Boxes are in pixels of `band`; OcrFrame width/height = band size.
    OcrFrame read(const BandImage& band, int64_t timeMs = 0, HybridDetail* detail = nullptr);

private:
    HybridOptions options_;
    WindowsOcrReader windows_;
    std::unique_ptr<PpOcrRecognizer> ppocr_;
    std::string ppocrStatus_;
};

}  // namespace crispplay::subs::ocr
