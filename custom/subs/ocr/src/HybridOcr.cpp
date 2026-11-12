#include "crispplay/subs/ocr/HybridOcr.h"

#include <algorithm>
#include <chrono>

#include "crispplay/subs/ocr/PpOcr.h"
#include "crispplay/subs/ocr/TextScore.h"

namespace crispplay::subs::ocr {

namespace {
double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

ReadingChoice chooseReading(std::string_view windowsText, std::string_view ppocrText, bool havePpocr, bool repair) {
    std::string w = repair ? repairReading(windowsText) : collapseSpaces(windowsText);
    if (!havePpocr) return {std::move(w), false};
    std::string p = repair ? repairReading(ppocrText) : collapseSpaces(ppocrText);
    if (wordiness(w) > wordiness(p)) return {std::move(w), false};
    return {std::move(p), true};
}

std::vector<TextRow> mergeRows(const std::vector<WinOcrLine>& lines) {
    std::vector<const WinOcrLine*> sorted;
    for (const auto& l : lines) sorted.push_back(&l);
    std::stable_sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
        return a->box.y + a->box.h / 2 < b->box.y + b->box.h / 2;
    });
    struct Group { float x0, y0, x1, y1; std::vector<const WinOcrLine*> members; };
    std::vector<Group> groups;
    for (const auto* l : sorted) {
        const float x0 = l->box.x, y0 = l->box.y, x1 = l->box.x + l->box.w, y1 = l->box.y + l->box.h;
        const float mid = (y0 + y1) / 2;
        if (!groups.empty() && groups.back().y0 <= mid && mid <= groups.back().y1) {
            auto& g = groups.back();
            g.x0 = std::min(g.x0, x0), g.y0 = std::min(g.y0, y0), g.x1 = std::max(g.x1, x1), g.y1 = std::max(g.y1, y1);
            g.members.push_back(l);
        } else {
            groups.push_back({x0, y0, x1, y1, {l}});
        }
    }
    std::vector<TextRow> rows;
    for (auto& g : groups) {
        std::stable_sort(g.members.begin(), g.members.end(), [](auto a, auto b) { return a->box.x < b->box.x; });
        TextRow row{{g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0}, {}};
        for (const auto* m : g.members) {
            if (!row.text.empty()) row.text.push_back(' ');
            row.text += m->text;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

Rect ppocrCrop(const Rect& row, int bandW, int bandH) {
    const float aw = float(bandW), ah = float(bandH);
    const float x0 = row.x, x1 = row.x + row.w, y0 = row.y, y1 = row.y + row.h, lh = row.h;
    float cx0, cx1;
    if (x0 < 0.6f * aw && x1 > 0.4f * aw) {
        // Centred row: symmetric about the band centre plus a margin.
        const float half = std::max(aw / 2 - x0, x1 - aw / 2) + 0.6f * lh;
        cx0 = std::max(0.f, aw / 2 - half);
        cx1 = std::min(aw, aw / 2 + half);
    } else {
        cx0 = std::max(0.f, x0 - 0.6f * lh);
        cx1 = std::min(aw, x1 + 0.6f * lh);
    }
    const float cy0 = std::max(0.f, y0 - 0.3f * lh), cy1 = std::min(ah, y1 + 0.3f * lh);
    // Integer pixel edges as numpy slicing in the spike (int() truncation).
    const float ix0 = float(int(cx0)), ix1 = float(int(cx1)), iy0 = float(int(cy0)), iy1 = float(int(cy1));
    return {ix0, iy0, std::max(0.f, ix1 - ix0), std::max(0.f, iy1 - iy0)};
}

HybridOcrReader::HybridOcrReader(HybridOptions options) : options_(std::move(options)), windows_(options_.language) {
    if (options_.ppocrModel.empty()) {
        ppocrStatus_ = "no PP-OCR model configured: Windows OCR only";
        return;
    }
    PpOcrOptions po;
    po.modelPath = options_.ppocrModel;
    po.runtimeDir = options_.runtimeDir;
    po.threads = options_.ppocrThreads;
    ppocr_ = PpOcrRecognizer::create(po, &ppocrStatus_);
    if (ppocr_) {
        ppocrStatus_.clear();
        (void)ppocr_->recognize(BandImage::make(320, 48));  // warm-up: first run allocates and optimises
    }
}

HybridOcrReader::~HybridOcrReader() = default;

bool HybridOcrReader::windowsReady() const { return windows_.ready(); }
bool HybridOcrReader::ppocrReady() const { return ppocr_ != nullptr; }
const WindowsOcrStatus& HybridOcrReader::windowsStatus() const { return windows_.status(); }
const std::string& HybridOcrReader::ppocrStatus() const { return ppocrStatus_; }

OcrFrame HybridOcrReader::read(const BandImage& band, int64_t timeMs, HybridDetail* detailOut) {
    const auto t0 = std::chrono::steady_clock::now();
    HybridDetail local;
    HybridDetail& detail = detailOut ? *detailOut : local;
    detail = {};
    OcrFrame frame;
    frame.timeMs = timeMs;
    frame.width = band.width;
    frame.height = band.height;
    if (band.empty()) return frame;

    WinOcrResult raw = windows_.read(band);
    detail.windowsMs += raw.copyMs + raw.recognizeMs;
    detail.error = raw.error;
    detail.rawLines = std::move(raw.lines);
    const std::vector<WinOcrLine>* found = &detail.rawLines;
    const bool rawEmpty = std::none_of(detail.rawLines.begin(), detail.rawLines.end(),
                                       [](const WinOcrLine& l) { return hasAlnum(l.text); });
    if (rawEmpty && options_.whiteTextFallback && raw.error.empty()) {
        WinOcrResult white = windows_.read(whiteTextOnly(band));
        detail.windowsMs += white.copyMs + white.recognizeMs;
        detail.usedWhiteFallback = true;
        detail.whiteLines = std::move(white.lines);
        found = &detail.whiteLines;
    }

    const auto t1 = std::chrono::steady_clock::now();
    for (const auto& row : mergeRows(*found)) {
        HybridLine line;
        line.windowsText = collapseSpaces(row.text);
        line.windowsBox = row.box;
        line.cropBox = ppocrCrop(row.box, band.width, band.height);
        if (ppocr_ && line.cropBox.w >= 1 && line.cropBox.h >= 1) {
            const auto& c = line.cropBox;
            const auto reading =
                ppocr_->recognize(crop(band, int(c.x), int(c.y), int(c.x + c.w), int(c.y + c.h)));
            line.ppocrText = collapseSpaces(reading.text);
            line.ppocrConfidence = reading.confidence;
        }
        const auto choice = chooseReading(line.windowsText, line.ppocrText, ppocr_ != nullptr, options_.repairText);
        line.ppocrChosen = choice.ppocr;
        if (!choice.text.empty()) {
            OcrLine out;
            out.text = choice.text;
            if (choice.ppocr) {
                // PP-OCR read the widened crop; keep the row's height, the crop's width.
                out.box = {line.cropBox.x, line.windowsBox.y, line.cropBox.w, line.windowsBox.h};
                out.confidence = line.ppocrConfidence;
            } else {
                out.box = line.windowsBox;
            }
            frame.lines.push_back(std::move(out));
        }
        detail.rows.push_back(std::move(line));
    }
    detail.ppocrMs = ppocr_ ? msSince(t1) : 0.0;
    detail.totalMs = msSince(t0);
    return frame;
}

}  // namespace crispplay::subs::ocr
