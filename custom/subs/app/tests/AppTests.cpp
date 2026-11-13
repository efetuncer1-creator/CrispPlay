// Unit tests of the subtitle app integration that need no GPU and no game
// content: band geometry and conversion, the present chain fan-out, the
// overlay picture (cover box, palette, missing glyphs).
#include <QtTest>

#include "BandFormat.h"
#include "OverlayRender.h"
#include "crispplay/present/Hooks.h"

#include <cstring>
#include <vector>

using namespace crispplay::subs;
using namespace crispplay::subs::app;

namespace {
struct FakeTap final : crispplay::present::Tap {
    bool take = true;
    int records = 0, submits = 0, discards = 0, ends = 0;
    bool record(ID3D12GraphicsCommandList*, const Frame&) override { ++records; return take; }
    void submitted(ID3D12Fence*, uint64_t) override { ++submits; }
    void discarded() override { ++discards; }
    void sessionEnded() override { ++ends; }
};
struct FakeRecorder final : veyra::engine::PresentCapture {
    bool take = false;
    int records = 0, submits = 0, discards = 0;
    bool record(ID3D12GraphicsCommandList*, const Frame&) override { ++records; return take; }
    void submitted(ID3D12Fence*, uint64_t) override { ++submits; }
    void discarded() override { ++discards; }
};
}

class AppTests : public QObject {
    Q_OBJECT
private slots:
    void defaultRegionIsTheSpikeBand() {
        const BandBox box = bandBox(Region{}, 1920, 1080);
        QCOMPARE(box.x, 268u);            // 14 %
        QCOMPARE(box.y, 670u);            // 62 %
        QCOMPARE(box.width, 1382u);       // to 86 %
        QCOMPARE(box.height, 378u);       // to 97 %
        QVERIFY(box.x % 2 == 0 && box.y % 2 == 0 && box.width % 2 == 0 && box.height % 2 == 0);
    }
    void regionIsClampedIntoTheFrame() {
        Region wild{-1.0f, 0.99f, 3.0f, 0.2f};
        const Region r = sanitized(wild);
        QVERIFY(r.left >= 0 && r.right <= 1 && r.top >= 0 && r.bottom <= 1);
        QVERIFY(r.right - r.left >= 0.099f && r.bottom - r.top >= 0.049f);
        for (const auto [w, h] : {std::pair{1920u, 1080u}, std::pair{3840u, 2160u}, std::pair{1281u, 721u}, std::pair{64u, 36u}}) {
            const BandBox box = bandBox(wild, w, h);
            QVERIFY(!box.empty());
            QVERIFY(box.x + box.width <= w && box.y + box.height <= h);
        }
        QVERIFY(bandBox(Region{}, 8, 8).empty());
    }
    void formats() {
        QCOMPARE(bytesPerPixel(DXGI_FORMAT_R8G8B8A8_UNORM), 4u);
        QCOMPARE(bytesPerPixel(DXGI_FORMAT_B8G8R8A8_UNORM), 4u);
        QCOMPARE(bytesPerPixel(DXGI_FORMAT_R10G10B10A2_UNORM), 4u);
        QCOMPARE(bytesPerPixel(DXGI_FORMAT_R16G16B16A16_FLOAT), 8u);
        QCOMPARE(bytesPerPixel(DXGI_FORMAT_NV12), 0u);
    }
    void rgba8RowsWithPitchBecomeBgra() {
        const uint32_t w = 3, h = 2, pitch = 256;
        std::vector<uint8_t> data(pitch * h, 0);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t* px = data.data() + y * pitch + x * 4;
                px[0] = uint8_t(10 + x); px[1] = uint8_t(20 + y); px[2] = 30; px[3] = 0;
            }
        const auto image = convertBand(data.data(), pitch, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        QCOMPARE(image.width, 3); QCOMPARE(image.height, 2);
        QCOMPARE(image.row(1)[2 * 4 + 0], uint8_t(30));   // B
        QCOMPARE(image.row(1)[2 * 4 + 1], uint8_t(21));   // G
        QCOMPARE(image.row(1)[2 * 4 + 2], uint8_t(12));   // R
        QCOMPARE(image.row(1)[2 * 4 + 3], uint8_t(255));
        QVERIFY(convertBand(data.data(), pitch, w, h, DXGI_FORMAT_NV12, 0).empty());
    }
    void pqToneMapIsMonotonicAndKeepsPaperWhiteBright() {
        int previous = -1;
        for (uint32_t code = 0; code < 1024; ++code) {
            const int v = pqCodeToSdr(code);
            QVERIFY(v >= previous);
            previous = v;
        }
        QCOMPARE(int(pqCodeToSdr(0)), 0);
        // 203 nits is PQ code ~0.58: white subtitles must come out near white, not grey.
        QVERIFY(pqCodeToSdr(594) >= 225);
        QVERIFY(pqCodeToSdr(1023) >= 250);
        // HDR10 path packs R in the low bits; SDR path of the same format stays linear.
        const uint32_t white = 594u | 594u << 10 | 594u << 20;
        const auto hdr = convertBand(&white, 256, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 2);
        const auto sdr = convertBand(&white, 256, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 0);
        QVERIFY(hdr.row(0)[0] >= 225 && hdr.row(0)[1] >= 225 && hdr.row(0)[2] >= 225);
        QVERIFY(sdr.row(0)[0] < 160);
    }
    void wideBandsAreHalvedForOcr() {
        auto narrow = ocr::BandImage::make(1384, 378);
        QCOMPARE(downscaleForOcr(narrow), 1);
        QCOMPARE(narrow.width, 1384);
        auto uhd = ocr::BandImage::make(2764, 756);
        QCOMPARE(downscaleForOcr(uhd), 2);
        QCOMPARE(uhd.width, 1382);
        auto huge = ocr::BandImage::make(5528, 1512);
        QCOMPARE(downscaleForOcr(huge), 4);
        QCOMPARE(huge.width, 1382);
    }
    void fingerprintFollowsThePicture() {
        auto a = ocr::BandImage::make(640, 160), b = ocr::BandImage::make(640, 160);
        QCOMPARE(fingerprint(a), fingerprint(b));
        std::memset(b.row(80), 200, size_t(b.stride));
        QVERIFY(fingerprint(a) != fingerprint(b));
        QCOMPARE(fingerprint(ocr::BandImage{}), uint64_t(0));
    }

    void presentChainFansOut() {
        using namespace crispplay::present;
        FakeRecorder recorder;
        QCOMPARE(wrapCapture(&recorder), static_cast<veyra::engine::PresentCapture*>(&recorder));   // no taps: untouched
        FakeTap first, second;
        QVERIFY(addTap(&first)); QVERIFY(addTap(&second)); QVERIFY(addTap(&first));
        auto* chain = wrapCapture(&recorder);
        QVERIFY(chain != &recorder);
        veyra::engine::PresentCapture::Frame frame;
        second.take = false;
        QVERIFY(chain->record(nullptr, frame));
        chain->submitted(nullptr, 7);
        QCOMPARE(recorder.records, 1); QCOMPARE(recorder.submits, 0);   // it recorded nothing
        QCOMPARE(first.records, 1); QCOMPARE(first.submits, 1);
        QCOMPARE(second.records, 1); QCOMPARE(second.submits, 0);
        recorder.take = true; first.take = false;
        QVERIFY(chain->record(nullptr, frame));
        chain->discarded();
        QCOMPARE(recorder.discards, 1); QCOMPARE(first.discards, 0); QCOMPARE(second.discards, 0);
        recorder.take = false;
        QVERIFY(!chain->record(nullptr, frame));
        sessionEnded();
        QCOMPARE(first.ends, 1); QCOMPARE(second.ends, 1);
        // Without a recorder the taps still run.
        auto* bare = wrapCapture(nullptr);
        first.take = true;
        QVERIFY(bare->record(nullptr, frame));
        bare->submitted(nullptr, 8);
        QCOMPARE(first.submits, 2);
    }

    void unknownGlyphsNeverReachThePainter() {
        const QString family = QStringLiteral("Segoe UI");
        QCOMPARE(displayable(QStringLiteral("Str ⁇ nzu"), family), QStringLiteral("Str nzu"));
        QCOMPARE(displayable(QStringLiteral("Güneş doğdu�"), family), QStringLiteral("Güneş doğdu"));
        QCOMPARE(displayable(QStringLiteral("İşçi ğüşöç ĞÜŞÖÇ ıİ"), family), QStringLiteral("İşçi ğüşöç ĞÜŞÖÇ ıİ"));
        QCOMPARE(displayable(QStringLiteral("a\tb\nc\u0007"), family), QStringLiteral("a b c"));
        // An unassigned code point: no font has it.
        const char32_t unassigned[] = {U'x', char32_t(0x378), U'y'};
        QCOMPARE(displayable(QString::fromUcs4(unassigned, 3), family), QStringLiteral("xy"));
    }
    void boxCoversTheWholeEnglishLine() {
        ShownLine line;
        line.speaker = "MINE GUARD"; line.text = "Kısa.";
        line.frameLeft = 0.25f; line.frameRight = 0.75f; line.frameTop = 0.80f; line.frameBottom = 0.90f;
        const auto frame = renderLine(line, 1920, 1080, OverlayStyle{});
        QVERIFY(frame);
        QCOMPARE(frame->width, 1920); QCOMPARE(frame->height, 1080);
        QCOMPARE(int(frame->images.size()), 1);
        const auto& image = frame->images.front();
        // The Turkish text is short, the box still spans the English union.
        QVERIFY(image.x <= 480 && image.x + image.width >= 1440);
        QVERIFY(image.y <= 864 && image.y + image.height >= 972);
        const auto pixels = image.pixels();
        QCOMPARE(pixels.size(), size_t(image.width) * size_t(image.height));
        // Opaque enough in the middle of the cover, and some bright text pixels exist.
        const uint32_t centre = pixels[size_t(image.height / 2) * image.width + 20];
        QVERIFY((centre >> 24) >= 0xE0);
        bool bright = false, yellow = false;
        for (const uint32_t p : pixels) {
            const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            if (r > 230 && g > 230 && b > 230) bright = true;
            if (r > 220 && g > 170 && b < 120) yellow = true;   // speaker colour
        }
        QVERIFY(bright); QVERIFY(yellow);
    }
    void longTextGrowsTheBoxButStaysOnThePicture() {
        ShownLine line;
        line.text = "Bu çok uzun bir çeviri satırı; İngilizce altyazıdan epeyce daha uzun olduğu için kutunun büyümesi, "
                    "gerekirse ikinci satıra geçmesi ve yine de görüntünün içinde kalması gerekir, başka çaresi yok.";
        line.frameLeft = 0.45f; line.frameRight = 0.55f; line.frameTop = 0.93f; line.frameBottom = 0.97f;
        const auto frame = renderLine(line, 1280, 720, OverlayStyle{});
        QVERIFY(frame);
        const auto& image = frame->images.front();
        QVERIFY(image.x >= 0 && image.y >= 0 && image.x + image.width <= 1280 && image.y + image.height <= 720);
        QVERIFY(image.width > 400);
        QVERIFY(image.x <= 576 && image.x + image.width >= 704);
        QVERIFY(!image.pixels().empty());
    }
    void nothingToDrawGivesNoPicture() {
        ShownLine line;
        line.text = "\xE2\x81\x87";   // only the unknown mark
        line.frameLeft = 0.3f; line.frameRight = 0.7f; line.frameTop = 0.8f; line.frameBottom = 0.9f;
        QVERIFY(!renderLine(line, 1920, 1080, OverlayStyle{}));
        line.text = "Merhaba";
        QVERIFY(!renderLine(line, 8, 8, OverlayStyle{}));
        // No geometry: still drawn, bottom centre.
        ShownLine loose; loose.text = "Merhaba";
        const auto frame = renderLine(loose, 1920, 1080, OverlayStyle{});
        QVERIFY(frame);
        QVERIFY(frame->images.front().y > 540);
    }
    void scaleChangesTheTextSize() {
        ShownLine line;
        line.text = "Nereye gidiyoruz?";
        OverlayStyle compact, large;
        compact.scale = 0.85; large.scale = 1.4;
        const auto a = renderLine(line, 1920, 1080, compact);
        const auto b = renderLine(line, 1920, 1080, large);
        QVERIFY(a && b);
        QVERIFY(b->images.front().height > a->images.front().height);
        QVERIFY(b->images.front().width > a->images.front().width);
    }
};

QTEST_MAIN(AppTests)
#include "AppTests.moc"
