#include "crispplay/discord/Controller.h"

#include "crispplay/Branding.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QSaveFile>
#include <QUrl>

namespace crispplay::discord {

namespace {
constexpr int kDebounceMs = 750;   // snapshotChanged can fire every frame; settle first
}

Controller::Controller(QObject* bridge, const QString& dataDirectory, const QString& clientId, QObject* parent)
    : QObject(parent), bridge_(bridge), clientId_(clientId) {
    settingsFile_ = QDir(dataDirectory).filePath(QStringLiteral("crispplay-discord.json"));
    settingsQml_ = QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                                       + QStringLiteral("/qml/CrispPlay/DiscordSettings.qml")).toString();
    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, &Controller::refresh);

    load();
    if (!appIdConfigured()) level_ = PrivacyLevel::Off;   // nothing to talk to

    if (bridge_ && bridge_->metaObject()->indexOfSignal("snapshotChanged()") >= 0)
        connect(bridge_, SIGNAL(snapshotChanged()), this, SLOT(onBridgeSnapshot()));
    if (level_ != PrivacyLevel::Off) debounce_.start();
}

Controller::~Controller() { shutdown(); }

void Controller::load() {
    QFile f(settingsFile_);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    level_ = levelFromString(o.value(QStringLiteral("level")).toString());
}

void Controller::save() const {
    QSaveFile f(settingsFile_);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                      {QStringLiteral("level"), levelToString(level_)}}).toJson());
    f.commit();
}

QString Controller::status() const {
    if (!appIdConfigured()) return QStringLiteral("Discord uygulama kimliği ayarlanmamış");
    if (level_ == PrivacyLevel::Off) return QString();
    switch (IpcClient::State(clientState_)) {
    case IpcClient::State::Connected: return QStringLiteral("Discord'a bağlı");
    case IpcClient::State::Searching: return QStringLiteral("Discord aranıyor (açık değilse bekleniyor)");
    case IpcClient::State::Idle: break;
    }
    return QStringLiteral("Hazır; bir kaynak açılınca görünür");
}

void Controller::setLevel(int value) {
    const auto next = value == 1 ? PrivacyLevel::Basic : value == 2 ? PrivacyLevel::Detailed : PrivacyLevel::Off;
    if (!appIdConfigured() || next == level_) return;
    level_ = next;
    save();
    emit levelChanged();
    refresh();
    emit statusChanged();
}

void Controller::onBridgeSnapshot() {
    if (level_ == PrivacyLevel::Off || debounce_.isActive()) return;   // cheap: one check per signal
    debounce_.start();
}

PresenceInput Controller::readInput() {
    PresenceInput in;
    in.level = level_;
    if (!bridge_) return in;
    in.source = sourceTypeFromBridgeKind(bridge_->property("sourceKind").toString());
    in.superResolution = bridge_->property("srActive").toBool();
    in.noiseReduction = bridge_->property("nrActive").toBool();
    in.frameGeneration = bridge_->property("fgActive").toBool();
    // Elapsed time runs from when this kind of source first appeared.
    if (in.source != trackedSource_) {
        trackedSource_ = in.source;
        trackedStart_ = in.source == SourceType::None ? 0 : QDateTime::currentSecsSinceEpoch();
    }
    in.startedAtUnix = trackedStart_;
    return in;
}

void Controller::ensureClient() {
    if (client_) return;
    IpcClient::Options opt;
    opt.clientId = clientId_;
    QPointer<Controller> self(this);
    client_ = std::make_unique<IpcClient>(opt, [self](IpcClient::State s) {
        // Worker thread: hand over to the UI thread.
        QMetaObject::invokeMethod(qApp, [self, s] {
            if (!self) return;
            self->clientState_ = int(s);
            emit self->statusChanged();
        }, Qt::QueuedConnection);
    });
    haveLast_ = false;
}

void Controller::refresh() {
    if (level_ == PrivacyLevel::Off || !appIdConfigured()) {
        shutdown();
        return;
    }
    const PresenceInput in = readInput();
    if (haveLast_ && in == last_ && client_) return;
    ensureClient();
    last_ = in;
    haveLast_ = true;
    client_->setActivity(buildActivity(in));
}

void Controller::shutdown() {
    debounce_.stop();
    if (client_) {
        client_->shutdown();   // clears the activity, bounded wait
        client_.reset();
    }
    haveLast_ = false;
    clientState_ = 0;
}

void install(QQmlContext* context, QObject* bridge, const QString& dataDirectory, QObject* parent) {
    auto* controller = new Controller(bridge, dataDirectory, branding::discordAppId(), parent);
    context->setContextProperty(QStringLiteral("crispDiscord"), controller);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, controller, &Controller::shutdown);
}

} // namespace crispplay::discord
