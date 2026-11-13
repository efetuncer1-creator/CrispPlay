#include "SubsController.h"

#include "OverlayRender.h"
#include "RegionEdit.h"
#include "SubtitleOverlay.h"
#include "crispplay/subs/app/Hooks.h"

#include "veyra/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QMetaProperty>
#include <QQuickImageProvider>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSaveFile>
#include <QVariant>

#include <algorithm>
#include <optional>
#include <cmath>
#include <format>
#include <thread>

namespace crispplay::subs::app {
namespace {

// The band that is copied and read (RegionEdit.cpp: default, wide, low, top,
// half, full). Smaller is cheaper and picks up less HUD text.
std::optional<Region> presetRegion(const QString& id) {
    if (const RegionPreset* preset = findRegionPreset(id.toLatin1().constData())) return toFraction(preset->region);
    return std::nullopt;
}

RegionPct pctOf(const QVariantMap& m) {
    return RegionPct{m.value(QStringLiteral("left"), 14).toDouble(), m.value(QStringLiteral("top"), 62).toDouble(),
                     m.value(QStringLiteral("right"), 86).toDouble(), m.value(QStringLiteral("bottom"), 97).toDouble()};
}
QVariantMap mapOf(const RegionPct& r) {
    return QVariantMap{{QStringLiteral("left"), r.left}, {QStringLiteral("top"), r.top},
                       {QStringLiteral("right"), r.right}, {QStringLiteral("bottom"), r.bottom}};
}
RegionPct pctOf(const RegionInt& r) { return RegionPct{double(r.left), double(r.top), double(r.right), double(r.bottom)}; }

// image://crispsubs/frame?<serial>
class FrameProvider final : public QQuickImageProvider {
public:
    explicit FrameProvider(std::shared_ptr<FrameStore> store) : QQuickImageProvider(QQuickImageProvider::Image), store_(std::move(store)) {}
    QImage requestImage(const QString&, QSize* size, const QSize& requested) override {
        QImage image;
        { std::lock_guard lock(store_->mutex); image = store_->image; }
        if (image.isNull()) { image = QImage(16, 9, QImage::Format_RGB32); image.fill(QColor(0x16, 0x16, 0x1A)); }
        if (size) *size = image.size();
        if (requested.width() > 0 && requested.height() > 0 && requested != image.size())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }
private:
    std::shared_ptr<FrameStore> store_;
};

// Theme.fontUi is "Noto Sans SC" for every language but ja / zh-TW. Windows
// does not ship it, and for a missing family Qt falls back to the default GUI
// font, MS Shell Dlg 2 = Tahoma. Tahoma's hinted 11 px "l" carries a wide right
// bearing, so with the menu headers' 0.44 px letter spacing "Altyazı" reads
// "Al tyazı" (and the same gap shows in other small text). Segoe UI is
// Windows' own UI face with the same metrics class; Microsoft YaHei UI keeps
// Chinese text covered. Nothing changes where Noto Sans SC is installed.
// CRISPPLAY_KEEP_UI_FONT=1 keeps Qt's fallback (before/after comparison).
void installInterfaceFont() {
    const QString wanted = QStringLiteral("Noto Sans SC");
    if (qEnvironmentVariableIsSet("CRISPPLAY_KEEP_UI_FONT") || QFontDatabase::hasFamily(wanted)) return;
    QFont::insertSubstitutions(wanted, {QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI")});
    veyra::log::info("subs", "interface font: Noto Sans SC is not installed; Segoe UI substitutes for it");
}
QString megabytes(qint64 bytes) { return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 0) + QStringLiteral(" MB"); }

// The overlay hook: called every subtitle tick on the UI thread.
class Overlay final : public OverlayBackend {
public:
    void appendLines(std::vector<veyra::ui::SubtitleLine>& draw, const veyra::ui::SubtitleView& view, const RECT& client) override {
        // Same mapping as the overlay's picture path: the canvas is the video's
        // rectangle on screen, so the picture is drawn 1:1 (sharp text).
        const double width = client.right, height = client.bottom;
        const double videoW = view.videoWidth > 0 ? view.videoWidth : width, videoH = view.videoHeight > 0 ? view.videoHeight : height;
        if (width < 32 || height < 32 || videoW <= 0 || videoH <= 0) { frame_.reset(); outline_.reset(); return; }
        const double fit = std::min(width / videoW, height / videoH) * view.preview.zoom;
        const int canvasW = int(std::lround(videoW * fit)), canvasH = int(std::lround(videoH * fit));

        // Region picker open: the edited region on the live picture.
        Region region;
        if (Service::instance().outline(region)) {
            if (!outline_ || !(region == outlineRegion_) || canvasW != outlineW_ || canvasH != outlineH_) {
                outline_ = renderOutline(region, canvasW, canvasH);
                outlineRegion_ = region; outlineW_ = canvasW; outlineH_ = canvasH;
            }
            if (outline_) { veyra::ui::SubtitleLine line; line.bitmap = outline_; draw.push_back(std::move(line)); }
        } else outline_.reset();

        DrawnLine drawn;
        if (!Service::instance().current(drawn)) { frame_.reset(); return; }
        if (!frame_ || serial_ != drawn.serial || canvasW != canvasW_ || canvasH != canvasH_) {
            OverlayStyle style;
            style.scale = drawn.scale; style.opacity = drawn.opacity;
            frame_ = renderLine(drawn.line, canvasW, canvasH, style);
            serial_ = drawn.serial; canvasW_ = canvasW; canvasH_ = canvasH;
        }
        if (!frame_) return;
        veyra::ui::SubtitleLine line;
        line.bitmap = frame_;   // text stays empty: game text does not go to the log
        draw.push_back(std::move(line));
    }
private:
    std::shared_ptr<const veyra::engine::SubtitleBitmapFrame> frame_, outline_;
    uint64_t serial_ = 0;
    int canvasW_ = 0, canvasH_ = 0, outlineW_ = 0, outlineH_ = 0;
    Region outlineRegion_;
};

} // namespace

SubsController::SubsController(const QString& dataDirectory, QObject* bridge, QObject* parent)
    : QObject(parent), dataDirectory_(dataDirectory), file_(QDir(dataDirectory).filePath(QStringLiteral("crispplay-subs.v1.json"))), bridge_(bridge) {
    models_ = new ModelStore(dataDirectory, this);
    connect(models_, &ModelStore::stateChanged, this, [this] { apply(false); });
    connect(models_, &ModelStore::progress, this, [this] { poll(); });
    connect(this, &SubsController::settingsChanged, this, &SubsController::rebuildMenu);
    connect(this, &SubsController::stateChanged, this, &SubsController::rebuildMenu);
    connect(this, &SubsController::onlineChanged, this, &SubsController::rebuildMenu);
    // The menu shows the toggle's shortcut: follow the bridge's shortcut table.
    if (bridge_) {
        const QMetaObject* meta = bridge_->metaObject();
        const int index = meta->indexOfProperty("shortcuts");
        if (index >= 0 && meta->property(index).hasNotifySignal())
            connect(bridge_, meta->property(index).notifySignal(), this, metaObject()->method(metaObject()->indexOfMethod("rebuildMenu()")));
    }
    load();
    onlineConfig_ = loadOnlineConfig(dataUtf8());
    onlineHasKey_ = onlineConfig_.provider != OnlineProvider::None && hasApiKey(dataUtf8(), onlineConfig_.provider);
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &SubsController::poll);
    apply(false);

    // Regression runs log the playback position with every event: keep it fresh.
    if (qEnvironmentVariableIsSet("CRISPPLAY_SUBS_EVENT_LOG") && bridge_) {
        auto* position = new QTimer(this);
        position->setInterval(40);
        connect(position, &QTimer::timeout, this, [this] { Service::instance().setPlaybackPosition(bridge_->property("position").toDouble()); });
        position->start();
    }

    // Test hook for real-pipeline runs (custom/scripts/subs-test.ps1): switch
    // effects on through the bridge, e.g. CRISPPLAY_TEST_SUBS_EFFECTS=sr+fg2.
    if (const QString effects = qEnvironmentVariable("CRISPPLAY_TEST_SUBS_EFFECTS"); !effects.isEmpty() && bridge_)
        QTimer::singleShot(2500, this, [this, effects] {
            if (effects.contains(QLatin1String("sr"))) bridge_->setProperty("srEnabled", true);
            if (const auto at = effects.indexOf(QLatin1String("fg")); at >= 0) {
                const int multiplier = at + 2 < effects.size() ? effects.at(at + 2).digitValue() : -1;
                if (multiplier >= 2) bridge_->setProperty("fgMultiplier", multiplier);
                bridge_->setProperty("fgEnabled", true);
            }
            veyra::log::info("subs", std::format("test effects={} sr={} fg={} x{}", effects.toStdString(),
                bridge_->property("srEnabled").toBool() ? 1 : 0, bridge_->property("fgEnabled").toBool() ? 1 : 0,
                bridge_->property("fgMultiplier").toInt()));
        });
}

SubsController::~SubsController() { Service::instance().shutdown(3000); }

bool SubsController::localBuilt() const { return CRISPPLAY_SUBS_HAVE_MT != 0; }

void SubsController::load() {
    QFile file(file_);
    if (!file.open(QIODevice::ReadOnly)) return;   // first run: defaults (off)
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    settings_.enabled = o.value("enabled").toBool(false);
    settings_.online = o.value("online").toBool(false);
    settings_.overlayScale = std::clamp(o.value("overlaySize").toInt(100), 70, 160) / 100.0;
    settings_.coverOpacity = std::clamp(o.value("coverOpacity").toInt(92), 60, 100);
    preset_ = o.value("regionPreset").toString(QStringLiteral("default"));
    if (const auto region = presetRegion(preset_)) settings_.region = *region;
    else {
        preset_ = QStringLiteral("custom");
        const QJsonObject r = o.value("region").toObject();
        Region custom;
        custom.left = float(r.value("left").toDouble(14) / 100.0); custom.top = float(r.value("top").toDouble(62) / 100.0);
        custom.right = float(r.value("right").toDouble(86) / 100.0); custom.bottom = float(r.value("bottom").toDouble(97) / 100.0);
        settings_.region = sanitized(custom);
    }
}

void SubsController::store() const {
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile file(file_);
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonObject o;
    o["enabled"] = settings_.enabled;
    o["online"] = settings_.online;
    o["overlaySize"] = overlaySize();
    o["coverOpacity"] = settings_.coverOpacity;
    o["regionPreset"] = preset_;
    QJsonObject r;
    r["left"] = regionLeft(); r["top"] = regionTop(); r["right"] = regionRight(); r["bottom"] = regionBottom();
    o["region"] = r;
    file.write(QJsonDocument(o).toJson());
    file.commit();
}

void SubsController::apply(bool persist) {
    Paths paths;
    paths.dataDirectory = dataDirectory_.toStdWString();
    // Only verified files are loaded: until the store says Ready there is no local model.
    if (models_->state() == ModelStore::State::Ready) {
        paths.translatorModel = models_->translatorModelDir().toStdWString();
        paths.ocrModel = models_->ocrModelPath().toStdWString();
    }
    Service::instance().configure(settings_, paths);
    if (persist) store();
    if (settings_.enabled) timer_.start();
    poll();
    emit settingsChanged();
}

void SubsController::setEnabled(bool enabled) { if (settings_.enabled != enabled) { settings_.enabled = enabled; apply(true); } }
void SubsController::setOnline(bool online) { if (settings_.online != online) { settings_.online = online; apply(true); } }
void SubsController::setOverlaySize(int percent) {
    percent = std::clamp(percent, 70, 160);
    if (overlaySize() != percent) { settings_.overlayScale = percent / 100.0; apply(true); }
}
void SubsController::setCoverOpacity(int percent) {
    percent = std::clamp(percent, 60, 100);
    if (settings_.coverOpacity != percent) { settings_.coverOpacity = percent; apply(true); }
}
void SubsController::setRegionPreset(const QString& preset) {
    if (preset == preset_) return;
    if (const auto region = presetRegion(preset)) { preset_ = preset; settings_.region = *region; }
    else preset_ = QStringLiteral("custom");   // keeps the current numbers as the starting point
    apply(true);
}
void SubsController::setRegion(int left, int top, int right, int bottom) {
    const RegionInt rounded = roundRegion(RegionPct{double(left), double(top), double(right), double(bottom)});
    const Region region = sanitized(toFraction(rounded));
    // A preset's numbers select the preset (the settings tab shows its name).
    const RegionPreset* preset = presetOf(rounded);
    const QString id = preset ? QString::fromLatin1(preset->id) : QStringLiteral("custom");
    if (preset_ == id && region == settings_.region) return;
    preset_ = id; settings_.region = region;
    apply(true);
}

void SubsController::toggle() {
    setEnabled(!settings_.enabled);
    if (!settings_.enabled) { emit notice(QStringLiteral("Altyazı çevirisi kapalı"), false); return; }
    const bool modelReady = models_->state() == ModelStore::State::Ready;
    if (!modelReady && !settings_.online)
        emit notice(QStringLiteral("Altyazı çevirisi açık, ama çeviri modeli yok · Ayarlar → Altyazı çevirisi'nden indirin"), true);
    else
        emit notice(QStringLiteral("Altyazı çevirisi açık · İngilizce → Türkçe (%1)").arg(settings_.online && onlineHasKey_ ? QStringLiteral("çevrimiçi") : QStringLiteral("yerel")), false);
}

void SubsController::saveOnline(int provider, const QString& endpoint, const QString& model) {
    OnlineConfig next = onlineConfig_;
    next.provider = provider == 1 ? OnlineProvider::DeepL : provider == 2 ? OnlineProvider::OpenAICompatible : OnlineProvider::None;
    next.endpoint = endpoint.trimmed().toStdString();
    next.model = model.trimmed().toStdString();
    if (next.provider == onlineConfig_.provider && next.endpoint == onlineConfig_.endpoint && next.model == onlineConfig_.model) return;
    if (!saveOnlineConfig(dataUtf8(), next)) { emit notice(QStringLiteral("Çevrimiçi çeviri ayarı kaydedilemedi"), true); return; }
    onlineConfig_ = next;
    onlineHasKey_ = next.provider != OnlineProvider::None && hasApiKey(dataUtf8(), next.provider);
    onlineTest_.clear();
    emit onlineChanged();
    if (settings_.online) Service::instance().reloadEngines();
}

void SubsController::saveApiKey(const QString& key) {
    if (onlineConfig_.provider == OnlineProvider::None) { emit notice(QStringLiteral("Önce bir çevrimiçi sağlayıcı seçin"), true); return; }
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) return;
    if (!crispplay::subs::saveApiKey(dataUtf8(), onlineConfig_.provider, trimmed.toStdString())) {
        emit notice(QStringLiteral("API anahtarı kaydedilemedi"), true);
        return;
    }
    onlineHasKey_ = hasApiKey(dataUtf8(), onlineConfig_.provider);
    onlineTest_.clear();
    emit onlineChanged();
    emit notice(QStringLiteral("API anahtarı kaydedildi (Windows hesabınıza şifreli)"), false);
    if (settings_.online) Service::instance().reloadEngines();
}

void SubsController::clearApiKey() {
    if (onlineConfig_.provider == OnlineProvider::None) return;
    crispplay::subs::clearApiKey(dataUtf8(), onlineConfig_.provider);
    onlineHasKey_ = false; onlineTest_.clear();
    emit onlineChanged();
    if (settings_.online) Service::instance().reloadEngines();
}

void SubsController::testOnline() {
    if (onlineTesting_) return;
    if (onlineConfig_.provider == OnlineProvider::None || !onlineHasKey_) {
        onlineTest_ = QStringLiteral("Sağlayıcı ve API anahtarı gerekli");
        emit onlineChanged();
        return;
    }
    onlineTesting_ = true; onlineTest_ = QStringLiteral("Deneniyor…");
    emit onlineChanged();
    const std::string data = dataUtf8();
    const OnlineConfig config = onlineConfig_;
    QPointer<SubsController> self(this);
    // Blocking network call: off the UI thread. Only this fixed sentence is sent.
    std::thread([self, data, config] {
        std::optional<std::string> result;
        if (auto translator = makeOnlineTranslator(data, config)) result = translator->translate("Where are we going?");
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, result] {
            if (!self) return;
            self->onlineTesting_ = false;
            self->onlineTest_ = result ? QStringLiteral("Çalışıyor: \"Where are we going?\" → \"%1\"").arg(QString::fromStdString(*result))
                                       : QStringLiteral("Başarısız: yanıt alınamadı (anahtar, adres, model ya da bağlantı)");
            emit self->onlineChanged();
        }, Qt::QueuedConnection);
    }).detach();
}

void SubsController::refresh() {
    Service::instance().probeOcr();
    timer_.start();
    poll();
}

void SubsController::poll() {
    if (bridge_) Service::instance().setPlaybackPosition(bridge_->property("position").toDouble());
    const Status s = Service::instance().status();

    QString models;
    switch (models_->state()) {
    case ModelStore::State::Ready:
        models = models_->usesDeveloperOverride() ? QStringLiteral("Hazır (geliştirici klasörü: %1)").arg(QDir::toNativeSeparators(models_->modelRoot()))
                                                  : QStringLiteral("Hazır · %1").arg(QDir::toNativeSeparators(models_->modelRoot()));
        break;
    case ModelStore::State::Downloading:
        models = models_->bytesTotal() > 0
            ? QStringLiteral("İndiriliyor: %1 / %2").arg(megabytes(models_->bytesDone()), megabytes(models_->bytesTotal()))
            : QStringLiteral("İndiriliyor…");
        break;
    case ModelStore::State::Failed: models = QStringLiteral("İndirilemedi: %1").arg(models_->error()); break;
    case ModelStore::State::Missing: {
        const qint64 size = builtInModelManifest().zipSize;
        if (models_->checking()) models = QStringLiteral("Model dosyaları denetleniyor…");
        else if (!models_->error().isEmpty()) models = QStringLiteral("Model dosyaları bozuk: %1").arg(models_->error());
        else models = size > 0 ? QStringLiteral("İndirilmedi · çeviri modeli (OPUS-MT) ve OCR ağı, tek dosya %1").arg(megabytes(size))
                               : QStringLiteral("Model dosyaları yok");
        break;
    }
    }

    QString ocr;
    if (s.ocrProbed && !s.ocrAvailable)
        ocr = QStringLiteral("Windows'ta kurulu OCR dili yok; altyazılar okunamaz. Ayarlar > Saat ve dil > Dil ve bölge > English (United States) > "
                             "Dil seçenekleri > Optik karakter tanıma'dan kurun.");
    else if (s.ocrProbed && !s.ocrEnglish)
        ocr = QStringLiteral("İngilizce OCR dil paketi kurulu değil; altyazılar okunamaz ya da hatalı okunur. Ayarlar > Saat ve dil > Dil ve bölge > "
                             "English (United States) > Dil seçenekleri > Optik karakter tanıma'dan kurun. "
                             "(PowerShell, yönetici: Add-WindowsCapability -Online -Name \"Language.OCR~~~en-US~0.0.1.0\")");

    QString engine;
    if (!localBuilt()) engine = QStringLiteral("Bu derlemede yerel çevirmen yok (CTranslate2 olmadan derlendi); yalnız çevrimiçi motor kullanılabilir.");
    else if (settings_.enabled && s.state == State::NoEngine && !s.localError.empty() && models_->state() == ModelStore::State::Ready)
        engine = QStringLiteral("Çeviri modeli yüklenemedi: %1").arg(QString::fromStdString(s.localError));
    if (settings_.online && !onlineHasKey_)
        engine += (engine.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("Çevrimiçi motor seçili ama API anahtarı yok; yerel motor kullanılıyor.");

    QString line;
    if (!settings_.enabled) line = QStringLiteral("Kapalı · kapalıyken görüntü okunmaz, ek yük yoktur");
    else switch (s.state) {
    case State::Off: case State::Starting: line = QStringLiteral("Başlatılıyor: OCR ve çeviri modeli yükleniyor…"); break;
    case State::NoOcr: line = QStringLiteral("Çalışmıyor: Windows OCR kullanılamıyor"); break;
    case State::NoEngine:
        line = models_->state() == ModelStore::State::Ready || !localBuilt() ? QStringLiteral("Çalışmıyor: çeviri motoru yok")
             : models_->checking() ? QStringLiteral("Başlatılıyor: model dosyaları denetleniyor…")
             : QStringLiteral("Çalışmıyor: çeviri modeli indirilmemiş · aşağıdan indirin");
        break;
    case State::Running:
        if (!s.samples) line = QStringLiteral("Açık · oynatma başlayınca altyazılar okunur");
        else {
            line = QStringLiteral("Çalışıyor · %1 · bant %2×%3 · OCR %4 ms · çeviri %5 ms · %6 satır gösterildi")
                .arg(s.onlineEngine ? QStringLiteral("çevrimiçi + yerel yedek") : QStringLiteral("yerel (OPUS-MT)"))
                .arg(s.bandWidth).arg(s.bandHeight).arg(int(s.ocrMs.p50 + 0.5)).arg(int(s.translateMs.p50 + 0.5)).arg(s.shown);
            if (!s.ppocr) line += QStringLiteral(" · yalnız Windows OCR (doğruluk düşük)");
            if (s.onlineFailed) line += QStringLiteral(" · çevrimiçi hata: %1").arg(s.onlineFailed);
        }
        break;
    }

    const bool changed = line != summary_ || ocr != ocrWarning_ || engine != engineWarning_ || models != modelSummary_ || int(s.state) != state_;
    summary_ = line; ocrWarning_ = ocr; engineWarning_ = engine; modelSummary_ = models; state_ = int(s.state);
    if (changed) emit stateChanged();
    if (!settings_.enabled && models_->state() != ModelStore::State::Downloading && !models_->checking() && s.state == State::Off) timer_.stop();
}

// ---- player bar menu ---------------------------------------------------------

namespace {
constexpr int kSizes[] = {85, 100, 120, 140};
constexpr int kOpacities[] = {75, 85, 92, 100};
QVariantMap item(std::initializer_list<std::pair<const char*, QVariant>> fields) {
    QVariantMap m;
    for (const auto& [k, v] : fields) m.insert(QString::fromLatin1(k), v);
    return m;
}
} // namespace

QString sizeLabel(int percent) {
    switch (percent) {
    case 85: return QStringLiteral("Küçük");
    case 100: return QStringLiteral("Normal");
    case 120: return QStringLiteral("Büyük");
    case 140: return QStringLiteral("Çok büyük");
    default: return QStringLiteral("%") + QString::number(percent);
    }
}
int nextSize(int percent) {
    for (const int v : kSizes) if (v > percent) return v;
    return kSizes[0];
}
int nextOpacity(int percent) {
    for (const int v : kOpacities) if (v > percent) return v;
    return kOpacities[0];
}

QVariantList SubsController::buildMenuItems(const MenuState& s) {
    QVariantList items;
    items << item({{"sep", true}});
    items << item({{"head", QStringLiteral("Altyazı çevirisi")}});

    // What keeps it from working, if anything (one disabled line; details in the settings tab).
    QString warning;
    if (s.localBuilt && !s.modelsReady && !s.onlineUsable) {
        if (s.modelState == 1)   // Downloading
            warning = s.bytesTotal > 0 ? QStringLiteral("Modeller indiriliyor: %%1").arg(int(100.0 * double(s.bytesDone) / double(s.bytesTotal)))
                                       : QStringLiteral("Modeller indiriliyor…");
        else if (s.modelState == 3) warning = QStringLiteral("Model indirilemedi — Ayarlar'dan yeniden dene");
        else warning = QStringLiteral("Modeller indirilmedi — Ayarlar'dan indir");
    } else if (s.ocrMissing) {
        warning = QStringLiteral("İngilizce OCR dil paketi yok — Ayarlar'a bak");
    }

    QString note;
    if (warning.isEmpty()) {
        const QString key = s.shortcut.isEmpty() ? QString() : QStringLiteral(" · ") + s.shortcut;
        if (!s.enabled) note = QStringLiteral("Kapalı") + key;
        else switch (s.serviceState) {
        case 2: note = QStringLiteral("Açık · İngilizce → Türkçe") + key; break;
        case 3: case 4: note = QStringLiteral("Açık ama çalışmıyor · ayrıntı Ayarlar'da"); break;
        default: note = QStringLiteral("Başlatılıyor…"); break;
        }
    }
    QVariantMap toggle = item({{"label", QStringLiteral("Türkçe çeviri")}, {"icon", QStringLiteral("globe")},
                               {"checked", s.enabled}, {"crisp", QStringLiteral("toggle")}});
    if (!note.isEmpty()) toggle.insert(QStringLiteral("note"), note);
    items << toggle;
    if (!warning.isEmpty()) items << item({{"label", warning}, {"disabled", true}, {"crisp", QStringLiteral("none")}});

    items << item({{"label", QStringLiteral("Yazı boyutu")}, {"icon", QStringLiteral("type")}, {"tag", sizeLabel(s.overlaySize)},
                   {"crisp", QStringLiteral("size")}});
    items << item({{"label", QStringLiteral("Kutu opaklığı")}, {"icon", QStringLiteral("eye")},
                   {"tag", QStringLiteral("%") + QString::number(s.coverOpacity)}, {"crisp", QStringLiteral("opacity")}});
    items << item({{"label", QStringLiteral("Çevrilecek alanı seç…")}, {"icon", QStringLiteral("scan")}, {"tag", s.regionLabel},
                   {"crisp", QStringLiteral("region")}});
    items << item({{"label", QStringLiteral("Çeviri ayarları…")}, {"icon", QStringLiteral("settings")},
                   {"crisp", QStringLiteral("settings")}, {"page", QStringLiteral("set")}});
    return items;
}

QString SubsController::shortcut() const {
    if (!bridge_) return {};
    return bridge_->property("shortcuts").toMap().value(QStringLiteral("subs")).toString();
}

void SubsController::rebuildMenu() {
    MenuState s;
    s.enabled = settings_.enabled;
    s.serviceState = state_;
    s.modelsReady = models_->state() == ModelStore::State::Ready;
    s.modelState = int(models_->state());
    s.bytesDone = models_->bytesDone(); s.bytesTotal = models_->bytesTotal();
    s.localBuilt = localBuilt();
    s.onlineUsable = settings_.online && onlineHasKey_;
    s.ocrMissing = !ocrWarning_.isEmpty();
    s.overlaySize = overlaySize(); s.coverOpacity = settings_.coverOpacity;
    s.regionLabel = preset_ == QLatin1String("custom") ? QStringLiteral("Özel") : presetLabel(preset_);
    s.shortcut = shortcut();
    QVariantList items = buildMenuItems(s);
    if (items == menuItems_) return;
    menuItems_ = std::move(items);
    emit menuChanged();
}

void SubsController::menuPicked(const QVariantMap& picked) {
    const QString what = picked.value(QStringLiteral("crisp")).toString();
    if (what == QLatin1String("toggle")) toggle();
    else if (what == QLatin1String("size")) {
        setOverlaySize(nextSize(overlaySize()));
        emit notice(QStringLiteral("Çeviri yazı boyutu: %1").arg(sizeLabel(overlaySize())), false);
    } else if (what == QLatin1String("opacity")) {
        setCoverOpacity(nextOpacity(settings_.coverOpacity));
        emit notice(QStringLiteral("Çeviri kutusu opaklığı: %%1").arg(settings_.coverOpacity), false);
    } else if (what == QLatin1String("region")) emit regionPickerRequested();
    else if (what == QLatin1String("settings")) emit settingsRequested();
    veyra::log::info("subs", std::format("menu pick {}", what.toStdString()));
}

// ---- region picker -----------------------------------------------------------

QString SubsController::regionLabel() const {
    if (preset_ != QLatin1String("custom")) return presetLabel(preset_);
    return QStringLiteral("Özel · sol %%1 · üst %%2 · sağ %%3 · alt %%4").arg(regionLeft()).arg(regionTop()).arg(regionRight()).arg(regionBottom());
}

QVariantList SubsController::regionPresetList() {
    QVariantList list;
    for (const RegionPreset& preset : regionPresets()) {
        QVariantMap m = mapOf(pctOf(preset.region));
        m.insert(QStringLiteral("id"), QString::fromLatin1(preset.id));
        m.insert(QStringLiteral("label"), QString::fromUtf8(preset.label));
        list << m;
    }
    return list;
}

QVariantMap SubsController::regionDrag(double x0, double y0, double x1, double y1) const { return mapOf(regionFromDrag(x0, y0, x1, y1)); }
QVariantMap SubsController::regionMove(const QVariantMap& region, double dx, double dy) const { return mapOf(moveRegion(pctOf(region), dx, dy)); }
QVariantMap SubsController::regionResize(const QVariantMap& region, int edges, double dx, double dy) const {
    return mapOf(resizeRegion(pctOf(region), edges, dx, dy));
}
QVariantMap SubsController::regionOfPreset(const QString& id) const {
    if (const RegionPreset* preset = findRegionPreset(id.toLatin1().constData())) return mapOf(pctOf(preset->region));
    return mapOf(RegionPct{});
}
QString SubsController::presetIdOf(const QVariantMap& region) const {
    const RegionPreset* preset = presetOf(roundRegion(pctOf(region)));
    return preset ? QString::fromLatin1(preset->id) : QStringLiteral("custom");
}
QString SubsController::presetLabel(const QString& id) const {
    if (const RegionPreset* preset = findRegionPreset(id.toLatin1().constData())) return QString::fromUtf8(preset->label);
    return QStringLiteral("Özel");
}
void SubsController::saveRegion(const QVariantMap& region) {
    const RegionInt r = roundRegion(pctOf(region));
    setRegion(r.left, r.top, r.right, r.bottom);
    veyra::log::info("subs", std::format("region saved {} {} {} {} preset={}", r.left, r.top, r.right, r.bottom, preset_.toStdString()));
}
void SubsController::setPreviewOutline(bool on, const QVariantMap& region) {
    Service::instance().setOutline(on, toFraction(roundRegion(pctOf(region))));
}

void SubsController::captureFrame() {
    if (frameBusy_) return;
    frameBusy_ = true;
    frameNote_ = QStringLiteral("Görüntü alınıyor…");
    emit frameChanged();
    Service::instance().requestSnapshot();
    QPointer<SubsController> self(this);
    std::shared_ptr<FrameStore> store = frameStore_;
    // Waiting for the GPU and converting a whole frame: off the UI thread.
    std::thread([self, store] {
        FrameSnapshot snap;
        const bool ok = Service::instance().takeSnapshot(snap, 1500);
        QImage image;
        if (ok) {
            const auto& b = snap.image;
            image = QImage(b.bgra.data(), b.width, b.height, b.stride, QImage::Format_RGB32).copy();
            constexpr int kWidth = 960;   // enough to place a rectangle; small to hold and draw
            if (image.width() > kWidth) image = image.scaledToWidth(kWidth, Qt::SmoothTransformation);
            std::lock_guard lock(store->mutex);
            store->image = image;
        }
        const int w = int(snap.frameWidth), h = int(snap.frameHeight);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, w, h] {
            if (!self) return;
            self->frameBusy_ = false;
            if (ok) {
                self->frameReady_ = true; self->frameWidth_ = w; self->frameHeight_ = h; ++self->frameSerial_;
                self->frameNote_.clear();
            } else {
                const bool source = self->bridge_ && self->bridge_->property("hasSource").toBool();
                self->frameNote_ = !source ? QStringLiteral("Görüntü yok: önce bir video ya da kaynak açın.")
                                 : self->frameReady_ ? QStringLiteral("Yeni kare alınamadı (video duraklatılmış olabilir); önceki kare gösteriliyor.")
                                                     : QStringLiteral("Kare alınamadı (video duraklatılmış olabilir). Oynatıp Yenile'ye basın.");
            }
            veyra::log::info("subs", std::format("snapshot {} {}x{}", ok ? "ok" : "none", w, h));
            emit self->frameChanged();
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace crispplay::subs::app

namespace crispplay::subs {

void installQml(QQmlEngine& engine, QObject* bridge, const std::filesystem::path& dataDirectory) {
    static app::Overlay overlay;   // process lifetime: the bridge may tick until the end
    installOverlay(&overlay);
    app::installInterfaceFont();
    // Parented to the application: destroyed (and the worker stopped) when it exits.
    auto* controller = new app::SubsController(QString::fromStdWString(dataDirectory.wstring()), bridge, QCoreApplication::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("crispSubs"), controller);
    engine.addImageProvider(QStringLiteral("crispsubs"), new app::FrameProvider(controller->frameStore()));   // the engine owns it
}

} // namespace crispplay::subs
