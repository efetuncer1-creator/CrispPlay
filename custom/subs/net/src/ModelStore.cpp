// Part of CrispPlay, GPL-3.0 (see LICENSE). Written for CrispPlay; the download/verify
// flow follows the idea of CouchSubs' ModelStore.cs (part file, resume, verify before use)
// but no code was taken from it.
#include <crispplay/subs/ModelStore.h>

#include "ZipReader.h"

#include "crispplay/Branding.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace crispplay::subs {

namespace detail {
struct GeneratedModelFile {
    const char* path;
    long long size;
    const char* sha256;
};
#include "ModelManifestData.inc"
} // namespace detail

const ModelManifest& builtInModelManifest() {
    static const ModelManifest manifest = [] {
        ModelManifest m;
        m.assetName = QString::fromLatin1(detail::kModelsAssetName);
        m.zipSize = detail::kModelsZipSize;
        m.zipSha256 = QByteArray(detail::kModelsZipSha256);
        for (const auto& f : detail::kModelsFiles)
            m.files.append({QString::fromUtf8(f.path), f.size, QByteArray(f.sha256)});
        return m;
    }();
    return manifest;
}

ModelStoreOptions defaultModelStoreOptions() {
    ModelStoreOptions o;
    o.url = branding::subsModelsUrl();
    o.manifest = builtInModelManifest();
    return o;
}

namespace {

constexpr auto kMtDir = "opus-mt-tc-big-en-tr-ct2-int8";
constexpr auto kOcrFile = "ppocr-rec/PP-OCRv6_rec_small.onnx";
constexpr auto kStampFile = ".crispplay-models.json";
constexpr auto kDownloadDir = ".download";

struct JobResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
};

bool isLoopbackHost(const QString& host) {
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") || host == QLatin1String("::1");
}

// GitHub serves release assets from github.com and redirects to one of its CDNs.
bool isGitHubCdn(const QString& host) {
    return host == QLatin1String("github.com") || host == QLatin1String("objects.githubusercontent.com")
        || host == QLatin1String("release-assets.githubusercontent.com");
}

bool schemeAllowed(const QUrl& url, bool allowLoopbackHttp) {
    if (!url.isValid() || !url.userInfo().isEmpty() || url.host().isEmpty()) return false;
    if (url.scheme() == QLatin1String("https")) return true;
    return allowLoopbackHttp && url.scheme() == QLatin1String("http") && isLoopbackHost(url.host());
}

bool redirectAllowed(const QUrl& original, const QUrl& target, bool allowLoopbackHttp) {
    if (!schemeAllowed(target, allowLoopbackHttp)) return false;
    const QString host = target.host().toLower();
    if (host == original.host().toLower()) return target.scheme() == QLatin1String("https") || isLoopbackHost(host);
    return target.scheme() == QLatin1String("https") && isGitHubCdn(host);
}

QByteArray hashFile(const QString& path, const std::atomic<bool>& cancel, bool* cancelled) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    QByteArray buffer(1 << 20, Qt::Uninitialized);
    for (;;) {
        if (cancel.load()) { *cancelled = true; return {}; }
        const qint64 n = f.read(buffer.data(), buffer.size());
        if (n < 0) return {};
        if (n == 0) break;
        h.addData(QByteArrayView(buffer.constData(), n));
    }
    return h.result().toHex();
}

JobResult verifyFiles(const QString& root, const ModelManifest& manifest, const std::atomic<bool>& cancel) {
    JobResult r;
    for (const ModelFileEntry& f : manifest.files) {
        const QString path = root + QLatin1Char('/') + f.path;
        const QFileInfo info(path);
        if (!info.isFile() || info.size() != f.size) {
            r.error = QStringLiteral("Model dosyası eksik ya da boyutu yanlış: %1").arg(f.path);
            return r;
        }
        bool cancelled = false;
        const QByteArray sha = hashFile(path, cancel, &cancelled);
        if (cancelled) { r.cancelled = true; return r; }
        if (sha != f.sha256) {
            r.error = QStringLiteral("Model dosyası bozuk (SHA-256 tutmuyor): %1").arg(f.path);
            return r;
        }
    }
    r.ok = true;
    return r;
}

QString firstSegment(const QString& path) { return path.section(QLatin1Char('/'), 0, 0); }

} // namespace

struct ModelStore::Impl {
    ModelStore* q = nullptr;
    QString dataDir;
    ModelStoreOptions opt;
    QString root;
    bool overrideRoot = false;

    State state = State::Missing;
    QString error;
    qint64 done = 0, total = 0;
    bool checking = false;

    QNetworkAccessManager* nam = nullptr;
    QNetworkReply* reply = nullptr;
    QFile part;
    qint64 resumeFrom = 0;
    bool statusChecked = false;
    QString netError;

    std::thread worker;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
    quint64 job = 0;

    QString downloadDir() const { return root + QLatin1Char('/') + QLatin1String(kDownloadDir); }
    QString partPath() const { return downloadDir() + QLatin1Char('/') + opt.manifest.assetName + QLatin1String(".part"); }
    QString stampPath() const { return root + QLatin1Char('/') + QLatin1String(kStampFile); }

    void setState(State s, const QString& err = {}) {
        state = s;
        error = err;
        emit q->stateChanged();
    }

    // --- background jobs ------------------------------------------------------------
    void stopWorker() {
        ++job; // results of an older job are ignored
        if (cancelFlag) cancelFlag->store(true);
        if (worker.joinable()) worker.join();
        cancelFlag.reset();
    }

    void startWorker(std::function<JobResult(const std::atomic<bool>&)> fn, std::function<void(const JobResult&)> onDone) {
        stopWorker();
        auto flag = std::make_shared<std::atomic<bool>>(false);
        cancelFlag = flag;
        const quint64 id = ++job;
        worker = std::thread([this, flag, fn = std::move(fn), onDone = std::move(onDone), id] {
            const JobResult r = fn(*flag);
            QMetaObject::invokeMethod(q, [this, r, onDone, id] {
                if (id != job) return;
                if (worker.joinable()) worker.join(); // the thread is finishing this very call
                cancelFlag.reset();
                onDone(r);
            }, Qt::QueuedConnection);
        });
    }

    // --- stamp: lets a restart skip re-hashing 260 MB when nothing changed --------------
    bool stampValid() const {
        QFile f(stampPath());
        if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) return false;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.value(QStringLiteral("zipSha256")).toString().toLatin1() != opt.manifest.zipSha256) return false;
        const QJsonArray files = o.value(QStringLiteral("files")).toArray();
        if (files.size() != opt.manifest.files.size()) return false;
        for (int i = 0; i < files.size(); ++i) {
            const QJsonObject e = files.at(i).toObject();
            const ModelFileEntry& m = opt.manifest.files.at(i);
            const QFileInfo info(root + QLatin1Char('/') + m.path);
            if (e.value(QStringLiteral("path")).toString() != m.path || !info.isFile() || info.size() != m.size
                || e.value(QStringLiteral("sha256")).toString().toLatin1() != m.sha256
                || qint64(e.value(QStringLiteral("mtime")).toDouble()) != info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch())
                return false;
        }
        return true;
    }

    void writeStamp() const {
        if (overrideRoot) return; // never write into a developer folder
        QJsonArray files;
        for (const ModelFileEntry& m : opt.manifest.files) {
            const QFileInfo info(root + QLatin1Char('/') + m.path);
            files.append(QJsonObject{{QStringLiteral("path"), m.path},
                                     {QStringLiteral("sha256"), QString::fromLatin1(m.sha256)},
                                     {QStringLiteral("mtime"), double(info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch())}});
        }
        QSaveFile f(stampPath());
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("zipSha256"), QString::fromLatin1(opt.manifest.zipSha256)},
                                          {QStringLiteral("files"), files}}).toJson());
        f.commit();
    }

    // --- check -------------------------------------------------------------------------
    void startCheck() {
        checking = true;
        emit q->stateChanged();
        const QString r = root;
        const ModelManifest m = opt.manifest;
        startWorker([r, m](const std::atomic<bool>& cancel) { return verifyFiles(r, m, cancel); },
                    [this](const JobResult& res) {
                        checking = false;
                        if (res.cancelled) { emit q->stateChanged(); return; }
                        if (res.ok) {
                            writeStamp();
                            done = total;
                            setState(State::Ready);
                            emit q->progress();
                        } else {
                            QFile::remove(stampPath());
                            // Missing is the normal "not downloaded yet" state; keep the reason
                            // only when something was there and is broken.
                            const bool anything = QFileInfo::exists(root + QLatin1Char('/') + QLatin1String(kMtDir))
                                || QFileInfo::exists(root + QLatin1Char('/') + firstSegment(QLatin1String(kOcrFile)));
                            setState(State::Missing, anything ? res.error : QString());
                        }
                    });
    }

    // --- download ----------------------------------------------------------------------
    void fail(const QString& text) {
        part.close();
        done = 0;
        emit q->progress();
        setState(State::Failed, text);
    }

    void startRequest(qint64 offset) {
        if (!nam) nam = new QNetworkAccessManager(q);
        part.setFileName(partPath());
        if (!part.open(QIODevice::WriteOnly | QIODevice::Append)) {
            fail(QStringLiteral("Geçici dosya yazılamadı: %1").arg(QDir::toNativeSeparators(partPath())));
            return;
        }
        QNetworkRequest req(opt.url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
        req.setMaximumRedirectsAllowed(5);
        req.setTransferTimeout(opt.stallTimeoutMs);
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("CrispPlay"));
        req.setRawHeader("Accept-Encoding", "identity"); // byte ranges must address the file itself
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        if (offset > 0) req.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
        resumeFrom = offset;
        statusChecked = false;
        netError.clear();
        reply = nam->get(req);
        QNetworkReply* r = reply;
        QObject::connect(r, &QNetworkReply::redirected, q, [this, r](const QUrl& url) {
            if (r != reply) return;
            const QUrl target = r->url().resolved(url);
            if (redirectAllowed(opt.url, target, opt.allowLoopbackHttp)) {
                emit r->redirectAllowed();
            } else {
                netError = QStringLiteral("İndirme beklenmeyen bir adrese yönlendirildi (%1); durduruldu.").arg(target.host());
                r->abort();
            }
        });
        QObject::connect(r, &QNetworkReply::readyRead, q, [this, r] { if (r == reply) onData(); });
        QObject::connect(r, &QNetworkReply::finished, q, [this, r] { if (r == reply) onFinished(); });
    }

    bool checkStatus() {
        statusChecked = true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 206 && resumeFrom > 0) {
            const QByteArray range = reply->rawHeader("Content-Range");
            if (!range.startsWith("bytes " + QByteArray::number(resumeFrom) + "-")) {
                netError = QStringLiteral("Sunucu yanlış bir parça gönderdi.");
                return false;
            }
            return true;
        }
        if (status == 200) {
            if (resumeFrom > 0) { // no resume support: start over
                part.resize(0);
                done = 0;
                resumeFrom = 0;
                emit q->progress();
            }
            return true;
        }
        netError = status ? QStringLiteral("Sunucu HTTP %1 döndürdü.").arg(status) : QString();
        return false;
    }

    void onData() {
        if (!statusChecked && !checkStatus()) { reply->abort(); return; }
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty()) return;
        if (done + chunk.size() > total) {
            netError = QStringLiteral("İndirilen dosya beklenen boyuttan büyük; durduruldu.");
            reply->abort();
            return;
        }
        if (part.write(chunk) != chunk.size()) {
            netError = QStringLiteral("Geçici dosya yazılamadı (disk dolu olabilir).");
            reply->abort();
            return;
        }
        done += chunk.size();
        emit q->progress();
    }

    void onFinished() {
        QNetworkReply* r = reply;
        if (r->error() == QNetworkReply::NoError && netError.isEmpty() && r->bytesAvailable() > 0) onData();
        if (r->error() == QNetworkReply::NoError && netError.isEmpty() && !statusChecked) checkStatus();
        reply = nullptr;
        r->deleteLater();
        part.close();
        if (!netError.isEmpty()) { fail(netError); return; }
        if (r->error() != QNetworkReply::NoError) {
            fail(r->error() == QNetworkReply::OperationCanceledError || r->error() == QNetworkReply::TimeoutError
                     ? QStringLiteral("Bağlantı zaman aşımına uğradı; yeniden denendiğinde kaldığı yerden sürer.")
                     : QStringLiteral("İndirme başarısız: %1").arg(r->errorString()));
            return;
        }
        if (done != total) {
            fail(QStringLiteral("İndirme eksik kaldı (%1 / %2 bayt); yeniden denendiğinde kaldığı yerden sürer.")
                     .arg(done).arg(total));
            return;
        }
        startInstall();
    }

    // --- verify, unpack, install (worker thread) ---------------------------------------
    static JobResult install(const QString& root, const QString& downloadDir, const QString& partPath,
                             const ModelManifest& manifest, const std::atomic<bool>& cancel) {
        JobResult r;
        bool cancelled = false;
        const QByteArray zipSha = hashFile(partPath, cancel, &cancelled);
        if (cancelled) { r.cancelled = true; return r; }
        if (zipSha != manifest.zipSha256) {
            QFile::remove(partPath);
            r.error = QStringLiteral("İndirilen model paketinin SHA-256 özeti tutmuyor; dosya silindi.");
            return r;
        }

        detail::ZipReader zip;
        QString zipError;
        if (!zip.open(partPath, &zipError)) {
            QFile::remove(partPath);
            r.error = QStringLiteral("Model paketi okunamadı: %1").arg(zipError);
            return r;
        }
        QHash<QString, detail::ZipEntry> byName;
        for (const detail::ZipEntry& e : zip.entries()) {
            if (e.directory) continue;
            byName.insert(e.name, e);
        }
        QSet<QString> wanted;
        for (const ModelFileEntry& f : manifest.files) wanted.insert(f.path);
        for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
            if (!wanted.contains(it.key())) {
                r.error = QStringLiteral("Model paketinde beklenmeyen dosya: %1").arg(it.key());
                return r;
            }
        }

        const QString staging = downloadDir + QStringLiteral("/staging");
        QDir(staging).removeRecursively();
        for (const ModelFileEntry& f : manifest.files) {
            const auto it = byName.constFind(f.path);
            if (it == byName.cend() || it->size != f.size) {
                r.error = QStringLiteral("Model paketinde dosya eksik ya da boyutu yanlış: %1").arg(f.path);
                return r;
            }
            const QString target = staging + QLatin1Char('/') + f.path;
            if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
                r.error = QStringLiteral("Klasör oluşturulamadı: %1").arg(QDir::toNativeSeparators(target));
                return r;
            }
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                r.error = QStringLiteral("Dosya yazılamadı: %1").arg(QDir::toNativeSeparators(target));
                return r;
            }
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (!zip.extract(*it, out, &h, &cancel, &zipError)) {
                if (cancel.load()) { r.cancelled = true; return r; }
                r.error = QStringLiteral("Model paketi açılamadı: %1").arg(zipError);
                return r;
            }
            out.close();
            if (h.result().toHex() != f.sha256) {
                r.error = QStringLiteral("Paketten çıkan dosyanın SHA-256 özeti tutmuyor: %1").arg(f.path);
                return r;
            }
        }
        if (cancel.load()) { r.cancelled = true; return r; }

        // Install: each top-level folder is swapped with two renames on the same volume.
        QStringList tops;
        for (const ModelFileEntry& f : manifest.files)
            if (!tops.contains(firstSegment(f.path))) tops.append(firstSegment(f.path));
        QDir rootDir(root);
        QStringList olds;
        for (const QString& top : tops) {
            const QString old = downloadDir + QStringLiteral("/old-") + top;
            QDir(old).removeRecursively();
            QFile::remove(old);
            const QString dst = root + QLatin1Char('/') + top;
            const bool had = QFileInfo::exists(dst);
            if (had && !rootDir.rename(dst, old)) {
                r.error = QStringLiteral("Eski model klasörü değiştirilemedi (kullanımda olabilir; uygulamayı yeniden başlatıp deneyin): %1")
                              .arg(QDir::toNativeSeparators(dst));
                return r;
            }
            if (!rootDir.rename(staging + QLatin1Char('/') + top, dst)) {
                if (had) rootDir.rename(old, dst);
                r.error = QStringLiteral("Model klasörü kurulamadı: %1").arg(QDir::toNativeSeparators(dst));
                return r;
            }
            if (had) olds.append(old);
        }
        for (const QString& old : olds) {
            QDir(old).removeRecursively();
            QFile::remove(old);
        }
        QDir(staging).removeRecursively();
        QFile::remove(partPath);
        r.ok = true;
        return r;
    }

    void startInstall() {
        const QString r = root, dd = downloadDir(), pp = partPath();
        const ModelManifest m = opt.manifest;
        startWorker([r, dd, pp, m](const std::atomic<bool>& cancel) { return install(r, dd, pp, m, cancel); },
                    [this](const JobResult& res) {
                        if (res.cancelled) return;
                        if (!res.ok) { fail(res.error); return; }
                        QDir(downloadDir()).rmdir(QStringLiteral("."));
                        writeStamp();
                        done = total;
                        emit q->progress();
                        setState(State::Ready);
                    });
    }
};

ModelStore::ModelStore(QString dataDirectory, QObject* parent)
    : ModelStore(std::move(dataDirectory), defaultModelStoreOptions(), parent) {}

ModelStore::ModelStore(QString dataDirectory, ModelStoreOptions options, QObject* parent)
    : QObject(parent), impl_(new Impl) {
    impl_->q = this;
    impl_->dataDir = std::move(dataDirectory);
    impl_->opt = std::move(options);
    impl_->total = impl_->opt.manifest.zipSize;
    const QString env = qEnvironmentVariable("CRISPPLAY_SUBS_MODEL_ROOT");
    if (!env.isEmpty() && QFileInfo(env).isDir()) {
        impl_->root = QDir::cleanPath(QFileInfo(env).absoluteFilePath());
        impl_->overrideRoot = true;
    } else {
        impl_->root = QDir::cleanPath(impl_->dataDir + QStringLiteral("/models"));
    }
    if (!impl_->overrideRoot && impl_->stampValid()) {
        impl_->state = State::Ready;
        impl_->done = impl_->total;
    } else {
        impl_->startCheck();
    }
}

ModelStore::~ModelStore() {
    if (impl_->reply) {
        QObject::disconnect(impl_->reply, nullptr, this, nullptr);
        impl_->reply->abort();
        impl_->reply->deleteLater();
        impl_->reply = nullptr;
    }
    impl_->stopWorker();
    delete impl_;
}

ModelStore::State ModelStore::state() const { return impl_->state; }
QString ModelStore::modelRoot() const { return impl_->root; }
QString ModelStore::translatorModelDir() const { return impl_->root + QLatin1Char('/') + QLatin1String(kMtDir); }
QString ModelStore::ocrModelPath() const { return impl_->root + QLatin1Char('/') + QLatin1String(kOcrFile); }
qint64 ModelStore::bytesDone() const { return impl_->done; }
qint64 ModelStore::bytesTotal() const { return impl_->total; }
QString ModelStore::error() const { return impl_->error; }
bool ModelStore::checking() const { return impl_->checking; }
bool ModelStore::usesDeveloperOverride() const { return impl_->overrideRoot; }

void ModelStore::download() {
    Impl& d = *impl_;
    if (d.state == State::Ready || d.state == State::Downloading) return;
    if (d.overrideRoot) {
        d.setState(State::Failed, QStringLiteral("CRISPPLAY_SUBS_MODEL_ROOT klasöründeki modeller manifestle eşleşmiyor; "
                                                 "geliştirici klasörüne indirme yapılmaz."));
        return;
    }
    d.stopWorker();
    d.checking = false;
    if (!schemeAllowed(d.opt.url, d.opt.allowLoopbackHttp)) {
        d.setState(State::Failed, QStringLiteral("Model adresi HTTPS değil; indirme yapılmadı."));
        return;
    }
    if (!QDir().mkpath(d.downloadDir())) {
        d.setState(State::Failed, QStringLiteral("Klasör oluşturulamadı: %1").arg(QDir::toNativeSeparators(d.downloadDir())));
        return;
    }
    QFile::remove(d.stampPath());
    qint64 existing = QFileInfo(d.partPath()).size();
    if (existing > d.total) {
        QFile::remove(d.partPath());
        existing = 0;
    }
    d.done = existing;
    emit progress();
    d.setState(State::Downloading);
    if (existing == d.total) d.startInstall(); // complete earlier, verify it
    else d.startRequest(existing);
}

void ModelStore::cancel() {
    Impl& d = *impl_;
    if (d.state != State::Downloading) return;
    if (d.reply) {
        QNetworkReply* r = d.reply;
        d.reply = nullptr;
        QObject::disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    d.part.close(); // the .part file stays: the next download() resumes it
    d.stopWorker();
    d.done = 0;
    emit progress();
    d.setState(State::Missing);
    d.startCheck(); // an install may have completed just before the cancel
}

void ModelStore::refresh() {
    if (impl_->state == State::Downloading) return;
    impl_->startCheck();
}

} // namespace crispplay::subs
