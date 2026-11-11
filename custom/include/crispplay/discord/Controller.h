// Owns the presence setting, watches the player bridge and feeds the IPC client.
// Exposed to QML as the `crispDiscord` context property.
#pragma once

#include "crispplay/discord/IpcClient.h"
#include "crispplay/discord/Presence.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>

class QQmlContext;

namespace crispplay::discord {

class Controller : public QObject {
    Q_OBJECT
    // 0 = Kapalı, 1 = Temel, 2 = Ayrıntılı
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)
    Q_PROPERTY(bool appIdConfigured READ appIdConfigured CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString settingsQml READ settingsQml CONSTANT)

public:
    // `bridge` is the QmlPlayerBridge (read through Qt properties and signals
    // only, so no upstream header is needed). `clientId` empty = feature off.
    Controller(QObject* bridge, const QString& dataDirectory, const QString& clientId, QObject* parent);
    ~Controller() override;

    int level() const { return int(level_); }
    void setLevel(int level);
    bool appIdConfigured() const { return isValidClientId(clientId_); }
    QString status() const;
    QString settingsQml() const { return settingsQml_; }

    // Clears the presence and stops the worker (called before the app exits).
    Q_INVOKABLE void shutdown();

signals:
    void levelChanged();
    void statusChanged();

private slots:
    void onBridgeSnapshot();
    void refresh();

private:
    PresenceInput readInput();
    void load();
    void save() const;
    void ensureClient();

    QPointer<QObject> bridge_;
    QString clientId_;
    QString settingsFile_;
    QString settingsQml_;
    PrivacyLevel level_ = PrivacyLevel::Off;
    std::unique_ptr<IpcClient> client_;
    QTimer debounce_;
    PresenceInput last_;
    bool haveLast_ = false;
    SourceType trackedSource_ = SourceType::None;
    qint64 trackedStart_ = 0;
    int clientState_ = 0;   // IpcClient::State as seen on the UI thread
};

// Creates the controller, publishes it as `crispDiscord` and hooks app exit.
// `parent` (the QApplication) owns it.
void install(QQmlContext* context, QObject* bridge, const QString& dataDirectory, QObject* parent);

} // namespace crispplay::discord
