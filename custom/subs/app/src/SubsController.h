// Game subtitle translation: the QML-facing object (`crispSubs`). UI thread only.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QImage>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <mutex>

#include "SubsService.h"
#include "crispplay/subs/ModelStore.h"
#include "crispplay/subs/OnlineTranslator.h"

namespace crispplay::subs::app {

// The still the region picker shows, shared by the controller (writer, UI
// thread) and the QML image provider (reader, scene graph thread).
struct FrameStore {
    std::mutex mutex;
    QImage image;
};

// Everything the player bar's menu section depends on (buildMenuItems).
struct MenuState {
    bool enabled = false;
    int serviceState = 0;          // State
    bool modelsReady = false, localBuilt = true, onlineUsable = false;
    int modelState = 0;            // ModelStore::State
    qint64 bytesDone = 0, bytesTotal = 0;
    bool ocrMissing = false;
    int overlaySize = 100, coverOpacity = 92;
    QString regionLabel, shortcut;
};
// Text sizes and box opacities the menu cycles through (same as the settings tab).
QString sizeLabel(int percent);
int nextSize(int percent);
int nextOpacity(int percent);

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
    // Test hook (CRISPPLAY_TEST_SUBS_UI=menu|picker|...): SubsHost.qml drives the UI for screenshots.
    Q_PROPERTY(QString testUi READ testUi CONSTANT)

    // Player bar: the "Altyazı çevirisi" section of the subtitle menu (VMenu
    // items; ours carry `crisp`, picked through menuPicked()).
    Q_PROPERTY(QVariantList menuItems READ menuItems NOTIFY menuChanged)
    // Region picker.
    Q_PROPERTY(QString regionLabel READ regionLabel NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList regionPresets READ regionPresetList CONSTANT)
    Q_PROPERTY(int frameSerial READ frameSerial NOTIFY frameChanged)       // image://crispsubs/frame?<serial>
    Q_PROPERTY(bool frameReady READ frameReady NOTIFY frameChanged)
    Q_PROPERTY(bool frameBusy READ frameBusy NOTIFY frameChanged)
    Q_PROPERTY(QString frameNote READ frameNote NOTIFY frameChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameChanged)         // source frame, pixels
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameChanged)
public:
    bool testOpenSettings() const { return qEnvironmentVariableIsSet("CRISPPLAY_TEST_SUBS_SETTINGS"); }
    QString testUi() const { return qEnvironmentVariable("CRISPPLAY_TEST_SUBS_UI"); }
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
    QVariantList menuItems() const { return menuItems_; }
    QString regionLabel() const;
    static QVariantList regionPresetList();
    int frameSerial() const { return frameSerial_; }
    bool frameReady() const { return frameReady_; }
    bool frameBusy() const { return frameBusy_; }
    QString frameNote() const { return frameNote_; }
    int frameWidth() const { return frameWidth_; }
    int frameHeight() const { return frameHeight_; }
    std::shared_ptr<FrameStore> frameStore() const { return frameStore_; }

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

    // Player bar menu: one of menuItems() was picked.
    Q_INVOKABLE void menuPicked(const QVariantMap& item);
    Q_INVOKABLE void openRegionPicker() { emit regionPickerRequested(); }
    Q_INVOKABLE void openSettings() { emit settingsRequested(); }
    // Region picker geometry (RegionEdit.h), percent of the frame; maps {left, top, right, bottom}.
    Q_INVOKABLE QVariantMap regionDrag(double x0, double y0, double x1, double y1) const;
    Q_INVOKABLE QVariantMap regionMove(const QVariantMap& region, double dx, double dy) const;
    Q_INVOKABLE QVariantMap regionResize(const QVariantMap& region, int edges, double dx, double dy) const;
    Q_INVOKABLE QVariantMap regionOfPreset(const QString& id) const;
    Q_INVOKABLE QString presetIdOf(const QVariantMap& region) const;
    Q_INVOKABLE QString presetLabel(const QString& id) const;
    // Saves a picked region (whole percent; a preset's numbers select that preset).
    Q_INVOKABLE void saveRegion(const QVariantMap& region);
    // Outline of the edited region on the video while the picker is open.
    Q_INVOKABLE void setPreviewOutline(bool on, const QVariantMap& region);
    // One still of the presented frame for the picker (asynchronous; frameChanged).
    Q_INVOKABLE void captureFrame();

    // Pure helpers (tests).
    static QVariantList buildMenuItems(const MenuState& state);

signals:
    void menuChanged();
    void frameChanged();
    void regionPickerRequested();
    void settingsRequested();
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
    Q_INVOKABLE void rebuildMenu();   // invokable: connected to the bridge's shortcutsChanged by meta method
    QString shortcut() const;
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
    QVariantList menuItems_;
    std::shared_ptr<FrameStore> frameStore_ = std::make_shared<FrameStore>();
    int frameSerial_ = 0, frameWidth_ = 0, frameHeight_ = 0;
    bool frameReady_ = false, frameBusy_ = false;
    QString frameNote_;
};


} // namespace crispplay::subs::app
