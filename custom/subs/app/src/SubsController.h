// Game subtitle translation: the QML-facing object (`crispSubs`). UI thread only.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <memory>

#include "SubsService.h"
#include "crispplay/subs/ModelStore.h"
#include "crispplay/subs/OnlineTranslator.h"

namespace crispplay::subs::app {

class SubsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    // Region of the picture that is read, whole percent of the frame.
    Q_PROPERTY(QString regionPreset READ regionPreset WRITE setRegionPreset NOTIFY settingsChanged)
    Q_PROPERTY(int regionLeft READ regionLeft NOTIFY settingsChanged)
    Q_PROPERTY(int regionTop READ regionTop NOTIFY settingsChanged)
    Q_PROPERTY(int regionRight READ regionRight NOTIFY settingsChanged)
    Q_PROPERTY(int regionBottom READ regionBottom NOTIFY settingsChanged)
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY settingsChanged)
    Q_PROPERTY(int overlaySize READ overlaySize WRITE setOverlaySize NOTIFY settingsChanged)       // percent, 70..160
    Q_PROPERTY(int coverOpacity READ coverOpacity WRITE setCoverOpacity NOTIFY settingsChanged)    // percent, 60..100
    // Online engine (crispplay_subs_net): 0 none, 1 DeepL, 2 OpenAI-compatible.
    Q_PROPERTY(int onlineProvider READ onlineProvider NOTIFY onlineChanged)
    Q_PROPERTY(QString onlineEndpoint READ onlineEndpoint NOTIFY onlineChanged)
    Q_PROPERTY(QString onlineModel READ onlineModel NOTIFY onlineChanged)
    Q_PROPERTY(bool onlineHasKey READ onlineHasKey NOTIFY onlineChanged)
    Q_PROPERTY(QString onlineTest READ onlineTest NOTIFY onlineChanged)   // result of the last test, Turkish
    Q_PROPERTY(bool onlineTesting READ onlineTesting NOTIFY onlineChanged)
    // Model files: ModelStore (state, bytesDone, bytesTotal, error; download(), cancel()).
    Q_PROPERTY(QObject* models READ models CONSTANT)
    Q_PROPERTY(QString modelSummary READ modelSummary NOTIFY stateChanged)
    Q_PROPERTY(bool localBuilt READ localBuilt CONSTANT)
    // One line for the settings page and warnings (Turkish; empty = nothing to warn about).
    Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)
    Q_PROPERTY(QString ocrWarning READ ocrWarning NOTIFY stateChanged)
    Q_PROPERTY(QString engineWarning READ engineWarning NOTIFY stateChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    // Test hook (CRISPPLAY_TEST_SUBS_SETTINGS=1): SubsHost.qml opens this settings tab at start.
    Q_PROPERTY(bool testOpenSettings READ testOpenSettings CONSTANT)
public:
    bool testOpenSettings() const { return qEnvironmentVariableIsSet("CRISPPLAY_TEST_SUBS_SETTINGS"); }
    SubsController(const QString& dataDirectory, QObject* bridge, QObject* parent = nullptr);
    ~SubsController() override;

    bool enabled() const { return settings_.enabled; }
    QString regionPreset() const { return preset_; }
    int regionLeft() const { return percent(settings_.region.left); }
    int regionTop() const { return percent(settings_.region.top); }
    int regionRight() const { return percent(settings_.region.right); }
    int regionBottom() const { return percent(settings_.region.bottom); }
    bool online() const { return settings_.online; }
    int overlaySize() const { return int(settings_.overlayScale * 100 + 0.5); }
    int coverOpacity() const { return settings_.coverOpacity; }
    int onlineProvider() const { return int(onlineConfig_.provider); }
    QString onlineEndpoint() const { return QString::fromStdString(onlineConfig_.endpoint); }
    QString onlineModel() const { return QString::fromStdString(onlineConfig_.model); }
    bool onlineHasKey() const { return onlineHasKey_; }
    QString onlineTest() const { return onlineTest_; }
    bool onlineTesting() const { return onlineTesting_; }
    QObject* models() const { return models_; }
    QString modelSummary() const { return modelSummary_; }
    bool localBuilt() const;
    QString summary() const { return summary_; }
    QString ocrWarning() const { return ocrWarning_; }
    QString engineWarning() const { return engineWarning_; }
    int state() const { return state_; }

    void setEnabled(bool enabled);
    void setRegionPreset(const QString& preset);
    void setOnline(bool online);
    void setOverlaySize(int percent);
    void setCoverOpacity(int percent);

    // Custom region in percent of the frame; switches the preset to "custom".
    Q_INVOKABLE void setRegion(int left, int top, int right, int bottom);
    // Hotkey: flips `enabled` and reports through notice().
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void saveOnline(int provider, const QString& endpoint, const QString& model);
    Q_INVOKABLE void saveApiKey(const QString& key);
    Q_INVOKABLE void clearApiKey();
    // Translates one fixed English sentence with the online engine (nothing else is sent).
    Q_INVOKABLE void testOnline();
    // The settings page opened: look for the OCR language pack, re-check the model files.
    Q_INVOKABLE void refresh();

signals:
    void settingsChanged();
    void onlineChanged();
    void stateChanged();
    void notice(const QString& message, bool error);

private:
    static int percent(float fraction) { return int(fraction * 100 + 0.5f); }
    void load();
    void store() const;
    void apply(bool persist);
    void poll();
    std::string dataUtf8() const { return dataDirectory_.toStdString(); }

    QString dataDirectory_, file_;
    QObject* bridge_ = nullptr;
    ModelStore* models_ = nullptr;
    Settings settings_;
    QString preset_ = QStringLiteral("default");
    OnlineConfig onlineConfig_;
    bool onlineHasKey_ = false, onlineTesting_ = false;
    QString onlineTest_, summary_, ocrWarning_, engineWarning_, modelSummary_;
    int state_ = 0;
    QTimer timer_;
};

} // namespace crispplay::subs::app
