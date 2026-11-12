// Phase 5 contract between the app integration (custom/subs/app) and the
// model download code (custom/subs/net). Implemented by crispplay_subs_net.
//
// Layout under <root>:
//   opus-mt-tc-big-en-tr-ct2-int8/   CTranslate2 model + source.spm/target.spm
//   ppocr-rec/                       PP-OCRv6_rec_small.onnx (+ dictionary)
// <root> is <dataDirectory>/models, unless CRISPPLAY_SUBS_MODEL_ROOT points
// at an existing folder (developer override, e.g. ..\deps\models).
#pragma once

#include <crispplay/subs/ModelManifest.h>

#include <QObject>
#include <QString>

namespace crispplay::subs {

class ModelStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int state READ stateValue NOTIFY stateChanged)
    Q_PROPERTY(qint64 bytesDone READ bytesDone NOTIFY progress)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progress)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    // True while the files on disk are being hashed (after construction without a valid
    // stamp, and after refresh()). state() stays Missing until the check says Ready.
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
public:
    enum class State { Missing = 0, Downloading = 1, Ready = 2, Failed = 3 };
    Q_ENUM(State)

    // Built-in source (GitHub release subs-models-v1) and manifest.
    explicit ModelStore(QString dataDirectory, QObject* parent = nullptr);
    // Same with another source/manifest (tests, future model sets).
    ModelStore(QString dataDirectory, ModelStoreOptions options, QObject* parent = nullptr);
    ~ModelStore() override;

    State state() const;
    int stateValue() const { return int(state()); }
    QString modelRoot() const;          // folder described above (may not exist yet)
    QString translatorModelDir() const; // <root>/opus-mt-tc-big-en-tr-ct2-int8
    QString ocrModelPath() const;       // <root>/ppocr-rec/PP-OCRv6_rec_small.onnx
    qint64 bytesDone() const;
    qint64 bytesTotal() const;
    QString error() const;
    bool checking() const;
    bool usesDeveloperOverride() const; // CRISPPLAY_SUBS_MODEL_ROOT is in effect

    // Downloads the pinned model release asset, verifies SHA-256 of every file,
    // installs atomically. No-op when Ready. Never blocks the caller.
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    // Re-checks the files on disk (hashes) and updates state().
    Q_INVOKABLE void refresh();

signals:
    void stateChanged();
    void progress();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace crispplay::subs
