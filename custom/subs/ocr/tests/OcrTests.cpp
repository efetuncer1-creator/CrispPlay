// Unit tests for crispplay_subs_ocr. No game content: images are drawn here.
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QtTest>

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <functional>
#include <thread>

#include "crispplay/subs/ocr/BandImage.h"
#include "crispplay/subs/ocr/HybridOcr.h"
#include "crispplay/subs/ocr/PpOcr.h"
#include "crispplay/subs/ocr/TextScore.h"
#include "crispplay/subs/ocr/WindowsOcr.h"

using namespace crispplay::subs;
using namespace crispplay::subs::ocr;

namespace {

uint16_t floatToHalf(float f) {
    // Enough for the test values (normal range, exact in half).
    uint32_t b;
    std::memcpy(&b, &f, 4);
    const uint32_t sign = (b >> 16) & 0x8000u;
    const int exp = int((b >> 23) & 0xFF) - 127 + 15;
    if ((b & 0x7FFFFFFFu) == 0) return uint16_t(sign);
    if ((b & 0x7F800000u) == 0x7F800000u) return uint16_t(sign | 0x7C00u | ((b & 0x7FFFFFu) ? 0x200u : 0u));
    if (exp >= 31) return uint16_t(sign | 0x7C00u);
    return uint16_t(sign | uint32_t(exp) << 10 | ((b >> 13) & 0x3FFu));
}

BandImage textBand(const QString& text, int w = 900, int h = 120, QColor fg = Qt::white, QColor bg = QColor(30, 30, 40)) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(bg);
    QPainter p(&img);
    QFont font("Segoe UI");
    font.setPixelSize(36);
    p.setFont(font);
    p.setPen(fg);
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    return fromBgra8(img.constBits(), w, h, int(img.bytesPerLine()));
}

std::filesystem::path modelPath() {
    const QByteArray env = qgetenv("CRISPPLAY_PPOCR_MODEL");
    return std::filesystem::path(env.isEmpty() ? std::string(CRISPPLAY_PPOCR_MODEL) : env.toStdString());
}

// The readers run on a worker thread (MTA), as in the app; the test's main
// thread is the GUI thread (STA) because QPainter text needs QGuiApplication.
void onWorker(const std::function<void()>& fn) {
    std::thread t(fn);
    t.join();
}

}  // namespace

class OcrTests : public QObject {
    Q_OBJECT
private slots:
    // ---- converters ----
    void rgba8Swaps() {
        const uint8_t px[8] = {10, 20, 30, 0, 200, 100, 50, 7};
        const BandImage img = fromRgba8(px, 2, 1, 8);
        QCOMPARE(img.stride, 8);
        QCOMPARE(int(img.bgra[0]), 30);
        QCOMPARE(int(img.bgra[1]), 20);
        QCOMPARE(int(img.bgra[2]), 10);
        QCOMPARE(int(img.bgra[3]), 255);
        QCOMPARE(int(img.bgra[4]), 50);
        QCOMPARE(int(img.bgra[6]), 200);
    }
    void bgra8HonoursStride() {
        uint8_t px[2 * 12] = {};
        px[12] = 1, px[13] = 2, px[14] = 3;  // first pixel of row 1 (stride 12)
        const BandImage img = fromBgra8(px, 2, 2, 12);
        QCOMPARE(img.stride, 8);
        QCOMPARE(int(img.row(1)[0]), 1);
        QCOMPARE(int(img.row(1)[2]), 3);
        QCOMPARE(int(img.row(1)[3]), 255);
    }
    void r10g10b10a2() {
        const uint32_t px[2] = {1023u | (512u << 10) | (0u << 20) | (3u << 30), 0u | (0u << 10) | (1023u << 20)};
        const BandImage img = fromR10G10B10A2(px, 2, 1, 8);
        QCOMPARE(int(img.bgra[2]), 255);  // R
        QCOMPARE(int(img.bgra[1]), 128);  // G 512/1023
        QCOMPARE(int(img.bgra[0]), 0);    // B
        QCOMPARE(int(img.bgra[4]), 255);  // second pixel blue
        QCOMPARE(int(img.bgra[7]), 255);
    }
    void halfFloat() {
        QCOMPARE(halfToFloat(0x3C00), 1.0f);
        QCOMPARE(halfToFloat(0xC000), -2.0f);
        QCOMPARE(halfToFloat(0x3800), 0.5f);
        QVERIFY(std::abs(halfToFloat(0x0001) - 5.96046448e-8f) < 1e-12f);  // smallest subnormal
        QVERIFY(std::isinf(halfToFloat(0x7C00)));
    }
    void rgba16fToneMap() {
        const float vals[5][3] = {{0, 0, 0}, {0.5f, 0.5f, 0.5f}, {1, 1, 1}, {8, 8, 0.5f}, {-1, NAN, 0.2f}};
        uint16_t px[5][4];
        for (int i = 0; i < 5; ++i) {
            for (int c = 0; c < 3; ++c) px[i][c] = floatToHalf(vals[i][c]);
            px[i][3] = floatToHalf(1);
        }
        const BandImage img = fromRgba16f(px, 5, 1, 40);
        const uint8_t* p = img.row(0);
        QCOMPARE(int(p[0]), 0);
        QCOMPARE(int(p[2]), 0);
        // 0.5 linear is below the knee: plain sRGB encode (188).
        QVERIFY(std::abs(int(p[4 + 2]) - 188) <= 1);
        // 1.0 is above the 0.8 knee: compressed but still bright, never 255.
        QVERIFY(p[8 + 2] > 220 && p[8 + 2] < 255);
        // Very bright yellow: saturates near white level, keeps R = G > B (hue survives).
        QVERIFY(p[12 + 2] >= 250);
        QCOMPARE(p[12 + 2], p[12 + 1]);
        QVERIFY(p[12 + 0] < 160);
        // Negative and NaN become 0, alpha forced.
        QCOMPARE(int(p[16 + 2]), 0);
        QCOMPARE(int(p[16 + 1]), 0);
        QCOMPARE(int(p[16 + 3]), 255);
        // HDR paper white: 2.0 scRGB with paperWhite 2 is the same as 1.0 with 1.
        uint16_t two[4] = {floatToHalf(2), floatToHalf(2), floatToHalf(2), floatToHalf(1)};
        HdrToneMap map;
        map.paperWhite = 2.f;
        QCOMPARE(int(fromRgba16f(two, 1, 1, 8, map).bgra[2]), int(p[8 + 2]));
    }
    void downscale() {
        BandImage img = BandImage::make(5, 3);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x) img.row(y)[4 * x] = uint8_t(10 * (x + 1) + y);
        const BandImage half = downscale2x(img);
        QCOMPARE(half.width, 2);
        QCOMPARE(half.height, 1);
        QCOMPARE(int(half.row(0)[0]), (10 + 20 + 11 + 21 + 2) / 4);
        QCOMPARE(int(half.row(0)[4]), (30 + 40 + 31 + 41 + 2) / 4);
        QCOMPARE(int(half.row(0)[3]), 255);
        QVERIFY(!needsDownscale(BandImage::make(1400, 10)));
        QVERIFY(needsDownscale(BandImage::make(1401, 10)));
    }
    void resizeMatchesOpenCvGeometry() {
        BandImage img = BandImage::make(2, 1);
        img.row(0)[0] = 0;
        img.row(0)[4] = 100;
        const BandImage up = resizeBilinear(img, 4, 1);
        // cv2.resize([[0, 100]], (4, 1)) -> [0, 25, 75, 100]
        QCOMPARE(int(up.row(0)[0]), 0);
        QCOMPARE(int(up.row(0)[4]), 25);
        QCOMPARE(int(up.row(0)[8]), 75);
        QCOMPARE(int(up.row(0)[12]), 100);
    }
    void whiteTextFallbackImage() {
        BandImage img = BandImage::make(20, 5);
        for (int x = 0; x < 20; ++x) img.row(2)[4 * x] = img.row(2)[4 * x + 1] = img.row(2)[4 * x + 2] = 0;    // dark
        for (int c = 0; c < 3; ++c) img.row(2)[4 * 3 + c] = 250, img.row(2)[4 * 18 + c] = 250;              // bright
        // Column 18 bright but surrounded by dark too; make a bright pixel with no dark nearby.
        BandImage distant = BandImage::make(30, 1);
        for (int c = 0; c < 3; ++c) distant.row(0)[4 * 0 + c] = 0, distant.row(0)[4 * 29 + c] = 250;
        for (int x = 1; x < 29; ++x) for (int c = 0; c < 3; ++c) distant.row(0)[4 * x + c] = 150;
        const BandImage a = whiteTextOnly(img);
        QCOMPARE(int(a.row(2)[4 * 3]), 0);    // text
        QCOMPARE(int(a.row(0)[4 * 3]), 255);  // dark background -> white
        const BandImage b = whiteTextOnly(distant);
        QCOMPARE(int(b.row(0)[4 * 29]), 255); // bright but no dark pixel within 5 px
    }

    // ---- text scoring ----
    void wordinessScores() {
        QCOMPARE(wordiness("Hello world"), 10);
        QCOMPARE(wordiness("\"Don't,\" she said."), 5 + 3 + 4);  // core "Don't" counts its apostrophe
    }
    void wordinessDetails() {
        QCOMPARE(wordiness("I'm"), 3);
        QCOMPARE(wordiness("STOP!"), 4);
        QCOMPARE(wordiness("42"), 2);
        QCOMPARE(wordiness("He11o"), -10);
        QCOMPARE(wordiness("rnay McDonald"), 4 - 16);
        QCOMPARE(wordiness("well\xE2\x80\xA6"), 4);              // trailing ellipsis
        QCOMPARE(wordiness("rock\xE2\x80\x99n"), 6);             // curly apostrophe join
        QCOMPARE(wordiness(""), 0);
        QVERIFY(wordiness("Take the money") > wordiness("Take tbe rn0ney"));
    }
    void repairs() {
        QCOMPARE(repairReading("l'm here, l think"), std::string("I'm here, I think"));
        QCOMPARE(repairReading("~* Hello"), std::string("Hello"));
        QCOMPARE(repairReading("\"Quote\""), std::string("\"Quote\""));
        QCOMPARE(repairReading("Go now Go now"), std::string("Go now"));
        QCOMPARE(repairReading("yes yes"), std::string("yes yes"));  // needs >= 2 words per half
        QCOMPARE(repairReading("level"), std::string("level"));
    }
    void choice() {
        QCOMPARE(chooseReading("Take the rn0ney", "Take the money", true, true).text, std::string("Take the money"));
        QVERIFY(chooseReading("Take the money", "Take tbe m0ney", true, true).ppocr == false);
        QVERIFY(chooseReading("Same", "Same", true, true).ppocr);     // tie -> PP-OCR
        QVERIFY(!chooseReading("Only windows", "", false, true).ppocr);
    }
    void rowsAndCrops() {
        std::vector<WinOcrLine> lines = {{"world", {300, 52, 100, 20}}, {"Hello", {150, 50, 120, 22}},
                                         {"second line", {200, 100, 300, 20}}};
        const auto rows = mergeRows(lines);
        QCOMPARE(int(rows.size()), 2);
        QCOMPARE(rows[0].text, std::string("Hello world"));
        QCOMPARE(rows[0].box.x, 150.f);
        QCOMPARE(rows[0].box.w, 250.f);
        // Centred row in a 1000 px band: symmetric about 500, plus 0.6 line heights.
        const Rect c = ppocrCrop({150, 50, 300, 22}, 1000, 200);
        QCOMPARE(c.x, float(int(500 - (350 + 0.6f * 22))));
        QCOMPARE(c.x + c.w, float(int(500 + (350 + 0.6f * 22))));
        QCOMPARE(c.y, float(int(50 - 0.3f * 22)));
        // Off-centre row only gets the margin.
        const Rect side = ppocrCrop({10, 10, 100, 20}, 1000, 200);
        QCOMPARE(side.x, 0.f);
        QCOMPARE(side.w, float(int(110 + 12)));
    }

    // ---- CTC ----
    void ctcDecode() {
        const std::vector<std::string> dict = {"a", "b", "\xC3\xBC"};  // + blank(0), space(4)
        const int C = 5;
        // steps: a a blank a b space b ü ü blank
        const int path[] = {1, 1, 0, 1, 2, 4, 2, 3, 3, 0};
        std::vector<float> probs(10 * C, 0.01f);
        for (int t = 0; t < 10; ++t) probs[size_t(t) * C + path[t]] = t == 4 ? 0.5f : 0.9f;
        const auto r = ctcGreedyDecode(probs.data(), 10, C, dict);
        QCOMPARE(r.text, std::string("aab b\xC3\xBC"));
        // kept steps: 0,3,4,5,6,7 -> (0.9*5 + 0.5)/6
        QVERIFY(std::abs(r.confidence - (0.9f * 5 + 0.5f) / 6) < 1e-5f);
        const std::vector<float> blank(3 * C, 0.f);
        std::vector<float> b2 = blank;
        for (int t = 0; t < 3; ++t) b2[size_t(t) * C] = 1.f;
        const auto e = ctcGreedyDecode(b2.data(), 3, C, dict);
        QVERIFY(e.text.empty());
        QCOMPARE(e.confidence, 0.f);
    }
    void ppocrInputShape() {
        int w = 0;
        // 20 px high crop: RapidOCR first scales the short side to 30 (rounded to 32), then to 48 high.
        const auto narrowIn = ppocrInput(BandImage::make(100, 20), 48, &w);
        QCOMPARE(w, 320);  // narrow: padded to 48 * 320 / 48
        QCOMPARE(int(narrowIn.size()), 3 * 48 * 320);
        QCOMPARE(narrowIn[0], -1.f);  // black -> -1
        const auto wide = ppocrInput(BandImage::make(1300, 48), 48, &w);
        QCOMPARE(w, 1300);
        QVERIFY(ppocrInput(BandImage{}, 48, &w).empty());
    }

    // ---- readers (need Windows OCR; PP-OCR parts skip without the model) ----
    void blankBandReadsNothing() {
        bool winReady = false;
        std::string why;
        OcrFrame f, none;
        HybridDetail d;
        onWorker([&] {
            HybridOptions o;
            o.ppocrModel = modelPath();
            HybridOcrReader reader(o);
            winReady = reader.windowsReady();
            why = reader.windowsStatus().message;
            if (!winReady) return;
            f = reader.read(BandImage::make(800, 200), 1234, &d);
            none = reader.read(BandImage{}, 0);
        });
        if (!winReady) QSKIP(why.c_str());
        QCOMPARE(f.timeMs, int64_t(1234));
        QCOMPARE(f.width, 800);
        QVERIFY(f.lines.empty());
        QVERIFY(d.usedWhiteFallback);  // an empty raw reading triggers the fallback
        QVERIFY(d.error.empty());
        QVERIFY(none.lines.empty());
    }
    void missingModelIsWindowsOnly() {
        const BandImage band = textBand("Where is the money");
        bool winReady = false, ppReady = true;
        std::string why, ppWhy;
        OcrFrame f;
        HybridDetail d;
        onWorker([&] {
            HybridOptions o;
            o.ppocrModel = L"Z:/does/not/exist/model.onnx";
            HybridOcrReader reader(o);
            winReady = reader.windowsReady();
            ppReady = reader.ppocrReady();
            why = reader.windowsStatus().message;
            ppWhy = reader.ppocrStatus();
            if (winReady) f = reader.read(band, 0, &d);
        });
        QVERIFY(!ppReady);
        QVERIFY(!ppWhy.empty());
        if (!winReady) QSKIP(why.c_str());
        QCOMPARE(int(f.lines.size()), 1);
        QCOMPARE(f.lines[0].text, std::string("Where is the money"));
        QVERIFY(d.rows[0].ppocrText.empty());
        QCOMPARE(d.ppocrMs, 0.0);
    }
    void hybridReadsDrawnText() {
        const BandImage band = textBand("You should have stayed home.");
        bool winReady = false, ppReady = false;
        std::string why;
        OcrFrame f;
        HybridDetail d;
        onWorker([&] {
            HybridOptions o;
            o.ppocrModel = modelPath();
            HybridOcrReader reader(o);
            winReady = reader.windowsReady();
            ppReady = reader.ppocrReady();
            why = winReady ? reader.ppocrStatus() : reader.windowsStatus().message;
            if (winReady && ppReady) f = reader.read(band, 0, &d);
        });
        if (!winReady || !ppReady) QSKIP(why.c_str());
        QCOMPARE(int(d.rows.size()), 1);
        QCOMPARE(d.rows[0].ppocrText, std::string("You should have stayed home."));
        QVERIFY(d.rows[0].ppocrConfidence > 0.8f);
        QCOMPARE(int(f.lines.size()), 1);
        QCOMPARE(f.lines[0].text, std::string("You should have stayed home."));
        const Rect& b = f.lines[0].box;
        QVERIFY(b.x > 0 && b.x + b.w < 900 && b.y > 20 && b.y + b.h < 100);
    }
    void refusesUiThreadApartment() {
        // A single-threaded apartment thread must be refused, not blocked.
        bool refused = false;
        std::thread t([&] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            WindowsOcrReader r;
            refused = !r.ready() && !WindowsOcrReader::prepareThread();
            CoUninitialize();
        });
        t.join();
        QVERIFY(refused);
    }
};

QTEST_MAIN(OcrTests)
#include "OcrTests.moc"
