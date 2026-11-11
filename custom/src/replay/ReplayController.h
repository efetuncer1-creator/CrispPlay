// Instant replay: the QML-facing object (`crispReplay`). UI thread only.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "crispplay/replay/ReplayService.h"

namespace crispplay::replay {

class ReplayController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int seconds READ seconds WRITE setSeconds NOTIFY settingsChanged)
    // Follows the recording codec setting (H.264 / HEVC); bound from QML.
    Q_PROPERTY(bool hevc READ hevc WRITE setHevc NOTIFY settingsChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY stateChanged)
    // capturing, unsupported, encoder, error, width, height, fps, bitrateMbps,
    // bufferedSeconds, bufferedMegabytes, dropped, audio
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    // One line for the settings page (Turkish).
    Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)
public:
    explicit ReplayController(const QString& settingsFile, QObject* parent = nullptr);
    ~ReplayController() override;

    bool enabled() const { return settings_.enabled; }
    int seconds() const { return settings_.seconds; }
    bool hevc() const { return settings_.hevc; }
    bool saving() const { return saving_; }
    QVariantMap state() const { return state_; }
    QString summary() const { return summary_; }
    void setEnabled(bool enabled);
    void setSeconds(int seconds);
    void setHevc(bool hevc);

    // Writes the last `seconds` into `directory`; the result arrives as saved() or failed().
    Q_INVOKABLE void save(const QString& directory);

signals:
    void settingsChanged();
    void stateChanged();
    void saved(const QString& fileName, const QString& path, double seconds);
    void failed(const QString& message);

private:
    void apply(bool persist);
    void load();
    void store() const;
    void poll();
    QString errorText(SaveError code, const QString& detail) const;

    QString file_;
    Settings settings_;
    QTimer timer_;
    QVariantMap state_;
    QString summary_;
    bool saving_ = false;
    uint64_t saved_ = 0, failures_ = 0;
};

} // namespace crispplay::replay
