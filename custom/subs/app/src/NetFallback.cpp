// Stand-in for crispplay_subs_net (model download + online engines), compiled
// only while that library is not part of the build. It implements the contract
// headers in custom/subs/net/include so the app code is the same either way:
// models are only looked for on disk (no download), there is no online engine.
#include "crispplay/subs/ModelStore.h"
#include "crispplay/subs/OnlineTranslator.h"

#include <QDir>
#include <QFileInfo>

namespace crispplay::subs {

struct ModelStore::Impl {
    QString root;
    State state = State::Missing;
    QString error;
};

ModelStore::ModelStore(QString dataDirectory, QObject* parent) : QObject(parent), impl_(new Impl) {
    const QString developerRoot = qEnvironmentVariable("CRISPPLAY_SUBS_MODEL_ROOT");
    impl_->root = !developerRoot.isEmpty() && QFileInfo(developerRoot).isDir() ? QDir::cleanPath(developerRoot)
                                                                              : QDir(dataDirectory).filePath(QStringLiteral("models"));
    refresh();
}
ModelStore::~ModelStore() { delete impl_; }

ModelStore::State ModelStore::state() const { return impl_->state; }
QString ModelStore::modelRoot() const { return impl_->root; }
QString ModelStore::translatorModelDir() const { return QDir(impl_->root).filePath(QStringLiteral("opus-mt-tc-big-en-tr-ct2-int8")); }
QString ModelStore::ocrModelPath() const { return QDir(impl_->root).filePath(QStringLiteral("ppocr-rec/PP-OCRv6_rec_small.onnx")); }
qint64 ModelStore::bytesDone() const { return 0; }
qint64 ModelStore::bytesTotal() const { return 0; }
QString ModelStore::error() const { return impl_->error; }

void ModelStore::download() {
    if (impl_->state == State::Ready) return;
    impl_->state = State::Failed;
    impl_->error = QStringLiteral("bu derlemede indirme bileşeni yok; model dosyalarını %1 klasörüne elle koyun").arg(QDir::toNativeSeparators(impl_->root));
    emit stateChanged();
}
void ModelStore::cancel() {}
void ModelStore::refresh() {
    const QDir translator(translatorModelDir());
    bool ready = QFileInfo::exists(ocrModelPath());
    for (const char* name : {"model.bin", "config.json", "source.spm", "target.spm"}) ready = ready && translator.exists(QLatin1String(name));
    const State next = ready ? State::Ready : State::Missing;
    if (next == impl_->state) return;
    impl_->state = next; impl_->error.clear();
    emit stateChanged();
}

OnlineConfig loadOnlineConfig(const std::string&) { return {}; }
bool saveOnlineConfig(const std::string&, const OnlineConfig&) { return false; }
bool saveApiKey(const std::string&, OnlineProvider, const std::string&) { return false; }
bool hasApiKey(const std::string&, OnlineProvider) { return false; }
void clearApiKey(const std::string&, OnlineProvider) {}
std::unique_ptr<Translator> makeOnlineTranslator(const std::string&, const OnlineConfig&) { return nullptr; }

} // namespace crispplay::subs
