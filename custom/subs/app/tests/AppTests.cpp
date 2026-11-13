// Unit tests of the subtitle app integration that need no game content:
// band geometry and conversion, the present chain fan-out, the overlay
// picture (cover box, palette, missing glyphs), the region picker's geometry,
// the player bar menu section, and the snapshot copy (on a WARP device).
#include <QtTest>

#include "BandCapture.h"
#include "BandFormat.h"
#include "OverlayRender.h"
#include "RegionEdit.h"
#include "SubsController.h"
#include "crispplay/present/Hooks.h"

#include <dxgi1_4.h>
#include <wrl/client.h>

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

class RegionTests : public QObject {
    Q_OBJECT
    static bool valid(const RegionPct& r) {
        return r.left >= 0 && r.top >= 0 && r.right <= 100 && r.bottom <= 100
            && r.right - r.left >= kRegionMinWidthPct - 1e-9 && r.bottom - r.top >= kRegionMinHeightPct - 1e-9;
    }
private slots:
    void dragGivesTheRectangleInEitherDirection() {
        QCOMPARE(regionFromDrag(10, 60, 90, 95), (RegionPct{10, 60, 90, 95}));
        QCOMPARE(regionFromDrag(90, 95, 10, 60), (RegionPct{10, 60, 90, 95}));   // dragged up-left
        QCOMPARE(regionFromDrag(80, 20, 30, 40), (RegionPct{30, 20, 80, 40}));
    }
    void dragIsClampedToTheFrame() {
        QCOMPARE(regionFromDrag(-20, 50, 130, 140), (RegionPct{0, 50, 100, 100}));
        const RegionPct r = regionFromDrag(std::nan(""), 10, 50, 40);
        QVERIFY(valid(r));
    }
    void tinyDragGrowsInItsDirection() {
        // A click or a short drag still makes a usable band, away from the start point.
        QCOMPARE(regionFromDrag(40, 70, 41, 71), (RegionPct{40, 70, 50, 75}));
        QCOMPARE(regionFromDrag(40, 70, 39, 69), (RegionPct{30, 65, 40, 70}));
        // At the edge it stays inside.
        QCOMPARE(regionFromDrag(98, 99, 99, 100), (RegionPct{90, 95, 100, 100}));
        QCOMPARE(regionFromDrag(2, 1, 1, 0), (RegionPct{0, 0, 10, 5}));
    }
    void moveKeepsTheSizeAndStopsAtTheEdges() {
        const RegionPct start{14, 62, 86, 97};
        QCOMPARE(moveRegion(start, 5, -10), (RegionPct{19, 52, 91, 87}));
        QCOMPARE(moveRegion(start, 50, 50), (RegionPct{28, 65, 100, 100}));
        QCOMPARE(moveRegion(start, -50, -80), (RegionPct{0, 0, 72, 35}));
    }
    void resizeMovesOnlyItsEdges() {
        const RegionPct start{14, 62, 86, 97};
        QCOMPARE(resizeRegion(start, EdgeLeft, -4, 99), (RegionPct{10, 62, 86, 97}));
        QCOMPARE(resizeRegion(start, EdgeRight | EdgeBottom, 10, 10), (RegionPct{14, 62, 96, 100}));
        QCOMPARE(resizeRegion(start, EdgeTop, -70, 0), (RegionPct{14, 62, 86, 97}));   // dy only
        QCOMPARE(resizeRegion(start, EdgeTop, 0, -70), (RegionPct{14, 0, 86, 97}));
        // Never past the opposite edge: the minimum size holds.
        QCOMPARE(resizeRegion(start, EdgeLeft, 90, 0), (RegionPct{76, 62, 86, 97}));
        QCOMPARE(resizeRegion(start, EdgeTop | EdgeLeft, 200, 200), (RegionPct{76, 92, 86, 97}));
        QCOMPARE(resizeRegion(start, EdgeBottom, 0, -90), (RegionPct{14, 62, 86, 67}));
    }
    void clampFixesAnythingStored() {
        QVERIFY(valid(clampRegion(RegionPct{120, -5, -30, 300})));
        QCOMPARE(clampRegion(RegionPct{50, 50, 52, 51}), (RegionPct{46, 48, 56, 53}));
        QCOMPARE(clampRegion(RegionPct{97, 98, 99, 99}), (RegionPct{90, 95, 100, 100}));
    }
    void roundingKeepsAValidRegion() {
        QCOMPARE(roundRegion(RegionPct{14.4, 61.6, 85.5, 97.2}), (RegionInt{14, 62, 86, 97}));
        // 0.4 + 10 rounds to a 10-wide band, not 9.
        const RegionInt r = roundRegion(RegionPct{20.4, 30.4, 30.4, 35.4});
        QVERIFY(r.right - r.left >= 10 && r.bottom - r.top >= 5);
        QCOMPARE(roundRegion(RegionPct{90.4, 95.4, 100, 100}), (RegionInt{90, 95, 100, 100}));
    }
    void presetsAreTheDocumentedOnes() {
        QStringList ids;
        for (const auto& preset : regionPresets()) {
            ids << QString::fromLatin1(preset.id);
            const RegionInt& r = preset.region;
            QCOMPARE(roundRegion(RegionPct{double(r.left), double(r.top), double(r.right), double(r.bottom)}), r);   // valid as stored
            QCOMPARE(presetOf(r), &preset);
        }
        QCOMPARE(ids, (QStringList{"default", "wide", "low", "top", "half", "full"}));
        QCOMPARE(findRegionPreset("default")->region, (RegionInt{14, 62, 86, 97}));
        QCOMPARE(findRegionPreset("half")->region, (RegionInt{0, 50, 100, 100}));
        QCOMPARE(findRegionPreset("full")->region, (RegionInt{0, 0, 100, 100}));
        QCOMPARE(findRegionPreset("top")->region, (RegionInt{14, 3, 86, 30}));
        QCOMPARE(QString::fromUtf8(findRegionPreset("half")->label), QStringLiteral("Alt yarı"));
        QVERIFY(!findRegionPreset("custom"));
        QVERIFY(!presetOf(RegionInt{15, 62, 86, 97}));
        // The default Region (fractions) is the default preset.
        const Region d = toFraction(findRegionPreset("default")->region);
        QCOMPARE(d, Region{});
    }
};

class MenuTests : public QObject {
    Q_OBJECT
    static QVariantMap find(const QVariantList& items, const QString& crisp) {
        for (const auto& v : items) if (v.toMap().value("crisp").toString() == crisp) return v.toMap();
        return {};
    }
private slots:
    void sectionShowsStateAndActions() {
        MenuState s;
        s.enabled = true; s.serviceState = 2; s.modelsReady = true; s.modelState = 2;
        s.overlaySize = 120; s.coverOpacity = 85; s.regionLabel = QStringLiteral("Alt yarı"); s.shortcut = QStringLiteral("F10");
        const QVariantList items = SubsController::buildMenuItems(s);
        QVERIFY(items.size() >= 7);
        QCOMPARE(items[0].toMap().value("sep").toBool(), true);
        QCOMPARE(items[1].toMap().value("head").toString(), QStringLiteral("Altyazı çevirisi"));
        const QVariantMap toggle = find(items, "toggle");
        QCOMPARE(toggle.value("checked").toBool(), true);
        QCOMPARE(toggle.value("label").toString(), QStringLiteral("Türkçe çeviri"));
        QCOMPARE(toggle.value("note").toString(), QStringLiteral("Açık · İngilizce → Türkçe · F10"));
        QCOMPARE(find(items, "size").value("tag").toString(), QStringLiteral("Büyük"));
        QCOMPARE(find(items, "opacity").value("tag").toString(), QStringLiteral("%85"));
        QCOMPARE(find(items, "region").value("tag").toString(), QStringLiteral("Alt yarı"));
        QCOMPARE(find(items, "region").value("label").toString(), QStringLiteral("Çevrilecek alanı seç…"));
        QCOMPARE(find(items, "settings").value("page").toString(), QStringLiteral("set"));
        QCOMPARE(find(items, "settings").value("label").toString(), QStringLiteral("Çeviri ayarları…"));
        QVERIFY(find(items, "none").isEmpty());   // nothing to warn about
        // Every one of ours is marked, so CineBar hands it to menuPicked().
        for (const auto& v : items) {
            const QVariantMap m = v.toMap();
            if (!m.contains("sep") && !m.contains("head")) QVERIFY(!m.value("crisp").toString().isEmpty());
            QVERIFY(!m.contains("act") && !m.contains("track"));   // never taken for upstream's own items
        }
    }
    void offAndMissingModels() {
        MenuState s;
        s.shortcut = QStringLiteral("F10");
        QCOMPARE(find(SubsController::buildMenuItems(s), "toggle").value("checked").toBool(), false);
        s.modelsReady = true;
        QCOMPARE(find(SubsController::buildMenuItems(s), "toggle").value("note").toString(), QStringLiteral("Kapalı · F10"));
        s.modelsReady = false; s.modelState = 0;
        QVariantList items = SubsController::buildMenuItems(s);
        QCOMPARE(find(items, "none").value("label").toString(), QStringLiteral("Modeller indirilmedi — Ayarlar'dan indir"));
        QCOMPARE(find(items, "none").value("disabled").toBool(), true);
        QVERIFY(!find(items, "toggle").contains("note"));   // one line of state, not two
        s.modelState = 1; s.bytesDone = 50; s.bytesTotal = 200;
        QCOMPARE(find(SubsController::buildMenuItems(s), "none").value("label").toString(), QStringLiteral("Modeller indiriliyor: %25"));
        // Online engine with a key: no local model needed.
        s.modelState = 0; s.onlineUsable = true;
        QVERIFY(find(SubsController::buildMenuItems(s), "none").isEmpty());
        s.onlineUsable = false; s.modelsReady = true; s.ocrMissing = true;
        QVERIFY(find(SubsController::buildMenuItems(s), "none").value("label").toString().contains("OCR"));
    }
    void sizeAndOpacityCycle() {
        QCOMPARE(nextSize(85), 100); QCOMPARE(nextSize(100), 120); QCOMPARE(nextSize(120), 140); QCOMPARE(nextSize(140), 85);
        QCOMPARE(nextSize(110), 120); QCOMPARE(nextSize(160), 85);
        QCOMPARE(nextOpacity(75), 85); QCOMPARE(nextOpacity(92), 100); QCOMPARE(nextOpacity(100), 75); QCOMPARE(nextOpacity(60), 75);
        QCOMPARE(sizeLabel(100), QStringLiteral("Normal")); QCOMPARE(sizeLabel(110), QStringLiteral("%110"));
    }
    void outlineSitsOnTheRegion() {
        const auto frame = renderOutline(Region{0.25f, 0.5f, 0.75f, 1.0f}, 1600, 900);
        QVERIFY(frame);
        const auto& image = frame->images.front();
        QCOMPARE(image.x, 400); QCOMPARE(image.y, 450);
        QCOMPARE(image.width, 800); QCOMPARE(image.height, 450);
        const auto pixels = image.pixels();
        QVERIFY((pixels[size_t(image.height / 2) * image.width + image.width / 2] >> 24) == 0);   // see-through inside
        QVERIFY((pixels[size_t(image.height / 2) * image.width + 1] >> 24) == 0xFF);              // the accent line
        QVERIFY(!renderOutline(Region{}, 8, 8));
    }
};

class SnapshotTests : public QObject {
    Q_OBJECT
private slots:
    // The one-shot full-frame copy on a software (WARP) device: what the picker
    // shows is the frame, in BGRA, while the band capture stays off.
    void snapshotCopiesTheWholeFrame() {
        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        ComPtr<ID3D12Device> device;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))
            || FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            QSKIP("no WARP device");
        const uint32_t w = 96, h = 54, pitch = 512;
        D3D12_HEAP_PROPERTIES gpu{}; gpu.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_PROPERTIES upload{}; upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC tex{};
        tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; tex.Width = w; tex.Height = h; tex.DepthOrArraySize = 1; tex.MipLevels = 1;
        tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM; tex.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> texture, staging;
        QVERIFY(SUCCEEDED(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))));
        D3D12_RESOURCE_DESC buf{};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width = pitch * h; buf.Height = 1; buf.DepthOrArraySize = 1; buf.MipLevels = 1;
        buf.SampleDesc.Count = 1; buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        QVERIFY(SUCCEEDED(device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging))));
        uint8_t* data = nullptr;
        QVERIFY(SUCCEEDED(staging->Map(0, nullptr, reinterpret_cast<void**>(&data))));
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t* px = data + y * pitch + x * 4;
                px[0] = uint8_t(x * 2); px[1] = uint8_t(y * 4); px[2] = 77; px[3] = 255;   // R G B A
            }
        staging->Unmap(0, nullptr);

        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue; ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence;
        QVERIFY(SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))));
        QVERIFY(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
        QVERIFY(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
        QVERIFY(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = staging.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &barrier);

        BandCapture capture;   // feature off: only the snapshot may record
        veyra::engine::PresentCapture::Frame frame;
        frame.texture = texture.Get(); frame.format = tex.Format; frame.width = w; frame.height = h;
        QVERIFY(!capture.record(list.Get(), frame));          // nothing asked for
        capture.requestSnapshot();
        frame.generated = true;
        QVERIFY(!capture.record(list.Get(), frame));          // generated frames are never taken
        frame.generated = false;
        QVERIFY(capture.record(list.Get(), frame));
        QVERIFY(SUCCEEDED(list->Close()));
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        QVERIFY(SUCCEEDED(queue->Signal(fence.Get(), 1)));
        capture.submitted(fence.Get(), 1);

        FrameSnapshot snap;
        QVERIFY(capture.takeSnapshot(snap, 3000));
        QCOMPARE(snap.frameWidth, w); QCOMPARE(snap.frameHeight, h);
        QCOMPARE(snap.image.width, int(w)); QCOMPARE(snap.image.height, int(h));
        const uint8_t* px = snap.image.row(10) + 20 * 4;      // BGRA
        QCOMPARE(int(px[0]), 77); QCOMPARE(int(px[1]), 40); QCOMPARE(int(px[2]), 40); QCOMPARE(int(px[3]), 255);
        QCOMPARE(capture.stats().recorded, uint64_t(0));       // no band sample was taken
        // One shot: without a new request nothing is copied, and nothing comes.
        QVERIFY(SUCCEEDED(allocator->Reset())); QVERIFY(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
        QVERIFY(!capture.record(list.Get(), frame));
        list->Close();
        QVERIFY(!capture.takeSnapshot(snap, 50));
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    int failed = 0;
    { AppTests t; failed += QTest::qExec(&t, argc, argv); }
    { RegionTests t; failed += QTest::qExec(&t, argc, argv); }
    { MenuTests t; failed += QTest::qExec(&t, argc, argv); }
    { SnapshotTests t; failed += QTest::qExec(&t, argc, argv); }
    return failed;
}
#include "AppTests.moc"
