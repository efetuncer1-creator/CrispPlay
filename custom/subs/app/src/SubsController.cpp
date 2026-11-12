#include "SubsController.h"

#include "OverlayRender.h"
#include "SubtitleOverlay.h"
#include "crispplay/subs/app/Hooks.h"

#include "veyra/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSaveFile>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <format>
#include <thread>

namespace crispplay::subs::app {
namespace {

struct Preset { const char* id; Region region; };
// The band that is copied and read. Smaller is cheaper and picks up less HUD text.
const Preset kPresets[] = {
    {"default", {0.14f, 0.62f, 0.86f, 0.97f}},   // lower third, centre
    {"wide",    {0.05f, 0.58f, 0.95f, 0.98f}},   // lower part, almost full width
    {"low",     {0.14f, 0.76f, 0.86f, 0.98f}},   // only the bottom edge
    {"top",     {0.14f, 0.03f, 0.86f, 0.30f}},   // subtitles at the top
};
const Region* presetRegion(const QString& id) {
    for (const auto& preset : kPresets) if (id == QLatin1String(preset.id)) return &preset.region;
    return nullptr;
}
QString megabytes(qint64 bytes) { return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 0) + QStringLiteral(" MB"); }

// The overlay hook: called every subtitle tick on the UI thread.
class Overlay final : public OverlayBackend {
public:
    void appendLines(std::vector<veyra::ui::SubtitleLine>& draw, const veyra::ui::SubtitleView& view, const RECT& client) override {
        DrawnLine drawn;
        if (!Service::instance().current(drawn)) { frame_.reset(); return; }
        // Same mapping as the overlay's picture path: the canvas is the video's
        // rectangle on screen, so the picture is drawn 1:1 (sharp text).
        const double width = client.right, height = client.bottom;
        const double videoW = view.videoWidth > 0 ? view.videoWidth : width, videoH = view.videoHeight > 0 ? view.videoHeight : height;
        if (width < 32 || height < 32 || videoW <= 0 || videoH <= 0) return;
        const double fit = std::min(width / videoW, height / videoH) * view.preview.zoom;
        const int canvasW = int(std::lround(videoW * fit)), canvasH = int(std::lround(videoH * fit));
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
    std::shared_ptr<const veyra::engine::SubtitleBitmapFrame> frame_;
    uint64_t serial_ = 0;
    int canvasW_ = 0, canvasH_ = 0;
};

} // namespace

SubsController::SubsController(const QString& dataDirectory, QObject* bridge, QObject* parent)
    : QObject(parent), dataDirectory_(dataDirectory), file_(QDir(dataDirectory).filePath(QStringLiteral("crispplay-subs.v1.json"))), bridge_(bridge) {
    models_ = new ModelStore(dataDirectory, this);
    connect(models_, &ModelStore::stateChanged, this, [this] { apply(false); });
    connect(models_, &ModelStore::progress, this, [this] { poll(); });
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
    if (const Region* region = presetRegion(preset_)) settings_.region = *region;
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
    paths.translatorModel = models_->translatorModelDir().toStdWString();
    paths.ocrModel = models_->ocrModelPath().toStdWString();
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
    if (const Region* region = presetRegion(preset)) { preset_ = preset; settings_.region = *region; }
    else preset_ = QStringLiteral("custom");   // keeps the current numbers as the starting point
    apply(true);
}
void SubsController::setRegion(int left, int top, int right, int bottom) {
    Region region;
    region.left = float(left) / 100.0f; region.top = float(top) / 100.0f; region.right = float(right) / 100.0f; region.bottom = float(bottom) / 100.0f;
    region = sanitized(region);
    if (preset_ == QLatin1String("custom") && region == settings_.region) return;
    preset_ = QStringLiteral("custom"); settings_.region = region;
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
    models_->refresh();
    timer_.start();
    poll();
}

void SubsController::poll() {
    if (bridge_) Service::instance().setPlaybackPosition(bridge_->property("position").toDouble());
    const Status s = Service::instance().status();

    QString models;
    switch (models_->state()) {
    case ModelStore::State::Ready: models = QStringLiteral("Model dosyaları hazır"); break;
    case ModelStore::State::Downloading:
        models = models_->bytesTotal() > 0
            ? QStringLiteral("İndiriliyor: %1 / %2").arg(megabytes(models_->bytesDone()), megabytes(models_->bytesTotal()))
            : QStringLiteral("İndiriliyor…");
        break;
    case ModelStore::State::Failed: models = QStringLiteral("İndirilemedi: %1").arg(models_->error()); break;
    case ModelStore::State::Missing:
        models = models_->bytesTotal() > 0 ? QStringLiteral("Model dosyaları yok (indirme boyutu %1)").arg(megabytes(models_->bytesTotal()))
                                           : QStringLiteral("Model dosyaları yok");
        break;
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
        line = models_->state() == ModelStore::State::Ready || !localBuilt()
            ? QStringLiteral("Çalışmıyor: çeviri motoru yok") : QStringLiteral("Çalışmıyor: çeviri modeli indirilmemiş");
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
    if (!settings_.enabled && models_->state() != ModelStore::State::Downloading && s.state == State::Off) timer_.stop();
}

} // namespace crispplay::subs::app

namespace crispplay::subs {

void installQml(QQmlEngine& engine, QObject* bridge, const std::filesystem::path& dataDirectory) {
    static app::Overlay overlay;   // process lifetime: the bridge may tick until the end
    installOverlay(&overlay);
    // Parented to the application: destroyed (and the worker stopped) when it exits.
    auto* controller = new app::SubsController(QString::fromStdWString(dataDirectory.wstring()), bridge, QCoreApplication::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("crispSubs"), controller);
}

} // namespace crispplay::subs
