#include "ReplayController.h"

#include "crispplay/replay/ReplayQml.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSaveFile>

#include <algorithm>

namespace crispplay::replay {
namespace {
// The durations the settings page offers.
int validSeconds(int seconds) {
    for (const int option : {15, 30, 60, 120}) if (seconds == option) return seconds;
    return 30;
}
}

ReplayController::ReplayController(const QString& settingsFile, QObject* parent) : QObject(parent), file_(settingsFile) {
    load();
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &ReplayController::poll);
    apply(false);
    const auto status = Service::instance().status();
    saved_ = status.saved; failures_ = status.saveFailures;

    // Test hook for a real-pipeline run: save once after N ms into a folder.
    //   CRISPPLAY_TEST_REPLAY_SAVE_MS=9000 CRISPPLAY_TEST_REPLAY_DIR=<folder>
    const int afterMs = qEnvironmentVariableIntValue("CRISPPLAY_TEST_REPLAY_SAVE_MS");
    const QString folder = qEnvironmentVariable("CRISPPLAY_TEST_REPLAY_DIR");
    if (afterMs > 0 && !folder.isEmpty()) QTimer::singleShot(afterMs, this, [this, folder] { save(folder); });
}

ReplayController::~ReplayController() { Service::instance().shutdown(3000); }

void ReplayController::load() {
    QFile file(file_);
    if (!file.open(QIODevice::ReadOnly)) return;   // first run: defaults (off)
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    settings_.enabled = o.value("enabled").toBool(false);
    settings_.seconds = validSeconds(o.value("seconds").toInt(30));
    settings_.audio = o.value("audio").toBool(true);
    settings_.fpsLimit = uint32_t(std::clamp(o.value("fpsLimit").toInt(60), 0, 240));
    settings_.maxMegabytes = uint32_t(std::clamp(o.value("maxMegabytes").toInt(512), 64, 4096));
}

void ReplayController::store() const {
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile file(file_);
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonObject o;
    o["enabled"] = settings_.enabled;
    o["seconds"] = settings_.seconds;
    o["audio"] = settings_.audio;
    o["fpsLimit"] = int(settings_.fpsLimit);
    o["maxMegabytes"] = int(settings_.maxMegabytes);
    file.write(QJsonDocument(o).toJson());
    file.commit();
}

void ReplayController::apply(bool persist) {
    Service::instance().configure(settings_);
    if (persist) store();
    // Polling only while there is something to show.
    if (settings_.enabled) timer_.start(); else if (!saving_) timer_.stop();
    poll();
    emit settingsChanged();
}

void ReplayController::setEnabled(bool enabled) { if (settings_.enabled != enabled) { settings_.enabled = enabled; apply(true); } }
void ReplayController::setSeconds(int seconds) {
    seconds = validSeconds(seconds);
    if (settings_.seconds != seconds) { settings_.seconds = seconds; apply(true); }
}
void ReplayController::setHevc(bool hevc) { if (settings_.hevc != hevc) { settings_.hevc = hevc; apply(false); } }

QString ReplayController::errorText(SaveError code, const QString& detail) const {
    switch (code) {
    case SaveError::Disabled: return QStringLiteral("Anlık tekrar kapalı · Ayarlar → Oynatma'dan açabilirsiniz");
    case SaveError::Empty: return QStringLiteral("Anlık tekrar tamponu boş · önce bir şey oynatın");
    case SaveError::Busy: return QStringLiteral("Önceki anlık tekrar hâlâ kaydediliyor");
    case SaveError::Directory: return QStringLiteral("Kayıt klasörü oluşturulamadı: %1").arg(detail);
    case SaveError::DiskSpace: return QStringLiteral("Kayıt klasöründe yeterli boş alan yok");
    case SaveError::Write: return QStringLiteral("Anlık tekrar kaydedilemedi: %1").arg(detail);
    case SaveError::None: break;
    }
    return {};
}

void ReplayController::save(const QString& directory) {
    const SaveError code = Service::instance().save(QDir::toNativeSeparators(directory).toStdWString());
    if (code != SaveError::None) {
        // Refusals are reported here; do not report them again from poll().
        failures_ = Service::instance().status().saveFailures;
        emit failed(errorText(code, directory));
        return;
    }
    timer_.start();
    poll();
}

void ReplayController::poll() {
    const Status s = Service::instance().status();
    QVariantMap next;
    next["capturing"] = s.capturing;
    next["unsupported"] = s.unsupported;
    next["encoder"] = QString::fromStdWString(s.encoder);
    next["error"] = QString::fromStdWString(s.error);
    next["width"] = s.width; next["height"] = s.height; next["fps"] = s.fps; next["bitrateMbps"] = s.bitrateMbps;
    next["bufferedSeconds"] = int(s.bufferedSeconds + 0.5);
    next["bufferedMegabytes"] = int(s.bufferedBytes >> 20);
    next["dropped"] = qulonglong(s.dropped);
    next["audio"] = s.audio;

    QString line;
    if (!s.enabled) line = QStringLiteral("Kapalıyken kodlayıcı çalışmaz, ek yük yoktur");
    else if (s.unsupported) line = QStringLiteral("Bu ekran kartında kullanılamıyor: NVIDIA (NVENC) gerekir");
    else if (s.capturing)
        line = QStringLiteral("Tamponda %1 sn · %2 MB · %3×%4 %5 fps · %6 Mbps%7")
                   .arg(next["bufferedSeconds"].toInt()).arg(next["bufferedMegabytes"].toInt())
                   .arg(s.width).arg(s.height).arg(s.fps).arg(s.bitrateMbps)
                   .arg(s.dropped ? QStringLiteral(" · atlanan kare: %1").arg(s.dropped) : QString());
    else if (!s.error.empty()) line = QStringLiteral("Kodlayıcı açılamadı, yeniden denenecek: %1").arg(QString::fromStdWString(s.error));
    else if (s.bufferedSeconds > 0) line = QStringLiteral("Oynatma durdu · tamponda %1 sn kaydedilebilir").arg(next["bufferedSeconds"].toInt());
    else line = QStringLiteral("Açık · oynatma başlayınca tampon dolmaya başlar");

    const bool changed = next != state_ || line != summary_ || saving_ != s.saving;
    state_ = next; summary_ = line; saving_ = s.saving;
    if (changed) emit stateChanged();

    if (s.saved != saved_) {
        saved_ = s.saved;
        const QString path = QString::fromStdWString(s.lastPath);
        emit saved(QFileInfo(path).fileName(), path, s.lastSeconds);
    }
    if (s.saveFailures != failures_) {
        failures_ = s.saveFailures;
        emit failed(errorText(s.lastError, QString::fromStdWString(s.lastErrorDetail)));
    }
    if (!settings_.enabled && !saving_) timer_.stop();
}

void installQml(QQmlEngine& engine, const std::filesystem::path& dataDirectory) {
    const QString file = QDir(QString::fromStdWString(dataDirectory.wstring())).filePath(QStringLiteral("crispplay-replay.v1.json"));
    // Parented to the application: destroyed (and the encoder stopped) when it exits.
    auto* controller = new ReplayController(file, QCoreApplication::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("crispReplay"), controller);
}

} // namespace crispplay::replay
