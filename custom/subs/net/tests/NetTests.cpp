// crispplay_subs_net_tests: model download/verify/unzip and the online translators,
// all against a local HTTP server (TestHttpServer). No real network calls.
#include "TestHttpServer.h"

#include "OnlineInternal.h"
#include "ZipReader.h"

#include <crispplay/subs/ModelStore.h>
#include <crispplay/subs/OnlineTranslator.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <zlib.h>

#include <thread>

using namespace crispplay::subs;

namespace {

QByteArray sha(const QByteArray& b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); }

void put16(QByteArray& b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }
void put32(QByteArray& b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }

struct ZipItem {
    QByteArray name, data;
    bool deflate = true;
};

QByteArray rawDeflate(const QByteArray& data) {
    uLongf size = compressBound(uLong(data.size()));
    QByteArray out(int(size), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(data.constData()),
                  uLong(data.size()), 6) != Z_OK)
        return {};
    return out.mid(2, int(size) - 6); // strip zlib header and adler32
}

QByteArray makeZip(const QList<ZipItem>& items) {
    QByteArray zip, central;
    for (const ZipItem& it : items) {
        const QByteArray payload = it.deflate ? rawDeflate(it.data) : it.data;
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef*>(it.data.constData()), uInt(it.data.size())));
        const quint32 offset = quint32(zip.size());
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0); put16(zip, it.deflate ? 8 : 0);
        put16(zip, 0); put16(zip, 0x21); put32(zip, crc); put32(zip, quint32(payload.size()));
        put32(zip, quint32(it.data.size())); put16(zip, quint16(it.name.size())); put16(zip, 0);
        zip += it.name;
        zip += payload;
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0);
        put16(central, it.deflate ? 8 : 0); put16(central, 0); put16(central, 0x21); put32(central, crc);
        put32(central, quint32(payload.size())); put32(central, quint32(it.data.size()));
        put16(central, quint16(it.name.size())); put16(central, 0); put16(central, 0); put16(central, 0);
        put16(central, 0); put32(central, 0); put32(central, offset);
        central += it.name;
    }
    const quint32 cdOffset = quint32(zip.size());
    zip += central;
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, quint16(items.size()));
    put16(zip, quint16(items.size())); put32(zip, quint32(central.size())); put32(zip, cdOffset); put16(zip, 0);
    return zip;
}

QByteArray noise(int size, quint32 seed) {
    QRandomGenerator rng(seed);
    QByteArray b(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) b[i] = char(rng.bounded(0, 40)); // compresses a little
    return b;
}

const QByteArray kMtPath = "opus-mt-tc-big-en-tr-ct2-int8/model.bin";
const QByteArray kOcrPath = "ppocr-rec/PP-OCRv6_rec_small.onnx";

struct Package {
    QList<ZipItem> items;
    QByteArray zip;
    ModelManifest manifest;
};

Package makePackage() {
    Package p;
    p.items = {{kMtPath, noise(600 * 1024, 1)},
               {"opus-mt-tc-big-en-tr-ct2-int8/config.json", "{\"a\":1}", false},
               {kOcrPath, noise(200 * 1024, 2)}};
    p.zip = makeZip(p.items);
    p.manifest.assetName = QStringLiteral("test-models.zip");
    p.manifest.zipSize = p.zip.size();
    p.manifest.zipSha256 = sha(p.zip);
    for (const ZipItem& it : p.items) p.manifest.files.append({QString::fromLatin1(it.name), it.data.size(), sha(it.data)});
    return p;
}

TestResponse serveRanges(const QByteArray& data, const TestRequest& req) {
    const QByteArray range = req.headers.value("range");
    if (range.startsWith("bytes=")) {
        const qint64 from = range.mid(6, range.indexOf('-') - 6).toLongLong();
        TestResponse r{206, {{"Content-Range", "bytes " + QByteArray::number(from) + "-" + QByteArray::number(data.size() - 1)
                                                   + "/" + QByteArray::number(data.size())}},
                       data.mid(from)};
        return r;
    }
    return TestResponse{200, {}, data};
}

ModelStoreOptions options(const QByteArray& url, const ModelManifest& m) {
    ModelStoreOptions o;
    o.url = QUrl(QString::fromLatin1(url));
    o.manifest = m;
    o.allowLoopbackHttp = true;
    o.stallTimeoutMs = 5000;
    return o;
}

std::string utf8(const QString& s) { return s.toStdString(); }

} // namespace

class NetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qunsetenv("CRISPPLAY_SUBS_MODEL_ROOT"); }

    // --- zip -------------------------------------------------------------------------------
    void zipNames() {
        QVERIFY(detail::isSafeZipName("a/b.txt"));
        QVERIFY(detail::isSafeZipName("dir/"));
        for (const char* bad : {"../evil.txt", "a/../../evil", "/abs.txt", "C:/x.txt", "a\\b.txt", "a//b", "./a", "a/.",
                                "con.txt", "dir/NUL", "trail. ", "x:y", ""})
            QVERIFY2(!detail::isSafeZipName(QString::fromLatin1(bad)), bad);
    }

    void zipTraversalRejected() {
        QTemporaryDir dir;
        for (const QByteArray name : {QByteArray("../evil.txt"), QByteArray("ok/../../evil.txt"), QByteArray("C:/evil.txt"),
                                      QByteArray("ok\\..\\evil.txt"), QByteArray("/evil.txt")}) {
            QFile f(dir.filePath("bad.zip"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(makeZip({{"ok.txt", "fine"}, {name, "pwned"}}));
            f.close();
            detail::ZipReader zip;
            QString error;
            QVERIFY2(!zip.open(f.fileName(), &error), name.constData());
            QVERIFY(error.contains(QLatin1String("unsafe")));
        }
        QVERIFY(!QFileInfo::exists(QDir(dir.path()).absoluteFilePath("../evil.txt")));
    }

    void zipExtractAndCrc() {
        QTemporaryDir dir;
        const QByteArray a = noise(100000, 7), b = "stored text";
        QByteArray zipBytes = makeZip({{"a.bin", a}, {"b.txt", b, false}});
        QFile f(dir.filePath("ok.zip"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(zipBytes);
        f.close();
        detail::ZipReader zip;
        QString error;
        QVERIFY2(zip.open(f.fileName(), &error), qPrintable(error));
        QCOMPARE(zip.entries().size(), 2);
        for (int i = 0; i < 2; ++i) {
            QBuffer out;
            out.open(QIODevice::WriteOnly);
            QCryptographicHash h(QCryptographicHash::Sha256);
            QVERIFY2(zip.extract(zip.entries()[i], out, &h, nullptr, &error), qPrintable(error));
            QCOMPARE(out.data(), i == 0 ? a : b);
            QCOMPARE(h.result().toHex(), sha(i == 0 ? a : b));
        }
        // Flip one stored byte: CRC must catch it.
        const qsizetype at = zipBytes.indexOf("stored text");
        zipBytes[at] = 'S';
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(zipBytes);
        f.close();
        QVERIFY(zip.open(f.fileName(), &error));
        QBuffer out;
        out.open(QIODevice::WriteOnly);
        QVERIFY(!zip.extract(zip.entries()[1], out, nullptr, nullptr, &error));
        QVERIFY(error.contains(QLatin1String("CRC")));
        // Not a zip at all.
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("hello world, definitely not an archive");
        f.close();
        QVERIFY(!zip.open(f.fileName(), &error));
    }

    // --- manifest --------------------------------------------------------------------------
    void builtInManifest() {
        const ModelManifest& m = builtInModelManifest();
        QCOMPARE(m.assetName, QStringLiteral("crispplay-subs-models-v1.zip"));
        QVERIFY(m.zipSize > 200'000'000);
        QCOMPARE(m.zipSha256.size(), 64);
        QStringList paths;
        for (const ModelFileEntry& f : m.files) {
            QCOMPARE(f.sha256.size(), 64);
            QVERIFY(f.size > 0);
            QVERIFY(detail::isSafeZipName(f.path));
            paths << f.path;
        }
        for (const char* need : {"opus-mt-tc-big-en-tr-ct2-int8/model.bin", "opus-mt-tc-big-en-tr-ct2-int8/config.json",
                                 "opus-mt-tc-big-en-tr-ct2-int8/shared_vocabulary.json", "opus-mt-tc-big-en-tr-ct2-int8/source.spm",
                                 "opus-mt-tc-big-en-tr-ct2-int8/target.spm", "opus-mt-tc-big-en-tr-ct2-int8/LICENSE-CC-BY-4.0.txt",
                                 "ppocr-rec/PP-OCRv6_rec_small.onnx", "ppocr-rec/LICENSE-Apache-2.0.txt"})
            QVERIFY2(paths.contains(QString::fromLatin1(need)), need);
        const ModelStoreOptions o = defaultModelStoreOptions();
        QCOMPARE(o.url.toString(), QStringLiteral(
            "https://github.com/efetuncer1-creator/CrispPlay/releases/download/subs-models-v1/crispplay-subs-models-v1.zip"));
        QVERIFY(!o.allowLoopbackHttp);
    }

    void storePaths() {
        QTemporaryDir dir;
        ModelStore store(dir.path(), options("http://127.0.0.1:1/x", makePackage().manifest));
        QTRY_VERIFY(!store.checking());
        QCOMPARE(store.state(), ModelStore::State::Missing);
        QVERIFY(store.error().isEmpty());
        QCOMPARE(store.modelRoot(), QDir::cleanPath(dir.path() + "/models"));
        QCOMPARE(store.translatorModelDir(), store.modelRoot() + "/opus-mt-tc-big-en-tr-ct2-int8");
        QCOMPARE(store.ocrModelPath(), store.modelRoot() + "/ppocr-rec/PP-OCRv6_rec_small.onnx");
    }

    // --- download ----------------------------------------------------------------------------
    void downloadGood() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest& r) { return serveRanges(p.zip, r); });
        QTemporaryDir dir;
        {
            ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
            QTRY_VERIFY(!store.checking());
            QSignalSpy progress(&store, &ModelStore::progress);
            store.download();
            QCOMPARE(store.state(), ModelStore::State::Downloading);
            QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 10000);
            QVERIFY(progress.count() > 0);
            QCOMPARE(store.bytesDone(), p.manifest.zipSize);
            QCOMPARE(store.bytesTotal(), p.manifest.zipSize);
            for (const ZipItem& it : p.items) {
                QFile f(store.modelRoot() + "/" + QString::fromLatin1(it.name));
                QVERIFY(f.open(QIODevice::ReadOnly));
                QCOMPARE(f.readAll(), it.data);
            }
            QVERIFY(!QFileInfo::exists(store.modelRoot() + "/.download/test-models.zip.part"));
            QVERIFY(!QFileInfo::exists(store.modelRoot() + "/.download/staging"));
            store.download(); // no-op when Ready
            QCOMPARE(server.requests().size(), 1);
            QCOMPARE(server.requests().first().method, QByteArray("GET"));
        }
        {
            // A restart finds the stamp: Ready at once, no hashing.
            ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
            QCOMPARE(store.state(), ModelStore::State::Ready);
            QVERIFY(!store.checking());
            // refresh() hashes again; a damaged file turns it Missing with a reason.
            QFile f(store.modelRoot() + "/" + QString::fromLatin1(kOcrPath));
            QVERIFY(f.open(QIODevice::ReadWrite));
            f.seek(10);
            f.write("X");
            f.close();
            store.refresh();
            QVERIFY(store.checking());
            QTRY_VERIFY(!store.checking());
            QCOMPARE(store.state(), ModelStore::State::Missing);
            QVERIFY(store.error().contains(QLatin1String("ppocr-rec")));
            // ... and download() repairs it (atomic replace of both folders).
            store.download();
            QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 10000);
            store.refresh();
            QTRY_VERIFY(!store.checking());
            QCOMPARE(store.state(), ModelStore::State::Ready);
        }
    }

    void downloadBadHash() {
        Package p = makePackage();
        p.manifest.zipSha256 = QByteArray(64, '0');
        TestHttpServer server([&](const TestRequest& r) { return serveRanges(p.zip, r); });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
        QVERIFY(store.error().contains(QLatin1String("SHA-256")));
        QVERIFY(!QFileInfo::exists(store.modelRoot() + "/.download/test-models.zip.part"));
        QVERIFY(!QFileInfo::exists(store.translatorModelDir()));
    }

    void downloadBadFileHash() {
        Package p = makePackage();
        p.manifest.files[2].sha256 = QByteArray(64, 'a'); // zip itself is fine, one file is not
        TestHttpServer server([&](const TestRequest& r) { return serveRanges(p.zip, r); });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
        QVERIFY(store.error().contains(QLatin1String("ppocr-rec")));
        QVERIFY(!QFileInfo::exists(store.ocrModelPath()));
    }

    void downloadTruncatedThenResume() {
        const Package p = makePackage();
        const qint64 half = p.zip.size() / 2;
        bool truncate = true;
        TestHttpServer server([&](const TestRequest& r) {
            TestResponse resp = serveRanges(p.zip, r);
            if (truncate) resp.sendBytes = half;
            return resp;
        });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
        const QString part = store.modelRoot() + "/.download/test-models.zip.part";
        QCOMPARE(QFileInfo(part).size(), half);
        truncate = false;
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 10000);
        const auto reqs = server.requests();
        QCOMPARE(reqs.size(), 2);
        QVERIFY(!reqs[0].headers.contains("range"));
        QCOMPARE(reqs[1].headers.value("range"), "bytes=" + QByteArray::number(half) + "-");
        QCOMPARE(reqs[1].headers.value("accept-encoding"), QByteArray("identity"));
    }

    void downloadTooLarge() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest&) { return TestResponse{200, {}, p.zip + QByteArray(1000, 'x')}; });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
        QVERIFY(QFileInfo(store.modelRoot() + "/.download/test-models.zip.part").size() <= p.manifest.zipSize);
    }

    void downloadHttpError() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest&) { return TestResponse{404, {}, "nope"}; });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
        QVERIFY(store.error().contains(QLatin1String("404")));
    }

    void redirectSameHostAllowed() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest& r) {
            if (r.path == "/asset.zip") return TestResponse{302, {{"Location", "/cdn/real.zip"}}, {}};
            return serveRanges(p.zip, r);
        });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 10000);
        QCOMPARE(server.requests().size(), 2);
        QCOMPARE(server.requests()[1].path, QByteArray("/cdn/real.zip"));
    }

    void redirectForeignHostRejected() {
        const Package p = makePackage();
        for (const QByteArray target : {QByteArray("https://evil.example.invalid/x.zip"),
                                        QByteArray("http://objects.githubusercontent.com/x.zip"),
                                        QByteArray("https://user:pw@objects.githubusercontent.com/x.zip")}) {
            TestHttpServer server([&](const TestRequest&) { return TestResponse{302, {{"Location", target}}, {}}; });
            QTemporaryDir dir;
            ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
            QTRY_VERIFY(!store.checking());
            store.download();
            QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 10000);
            QVERIFY2(store.error().contains(QStringLiteral("yönlendirildi")), qPrintable(store.error()));
            QCOMPARE(server.requests().size(), 1);
        }
    }

    void httpsOnly() {
        ModelStoreOptions o = options("http://example.com/x.zip", makePackage().manifest);
        o.allowLoopbackHttp = false;
        QTemporaryDir dir;
        ModelStore store(dir.path(), o);
        QTRY_VERIFY(!store.checking());
        store.download();
        QCOMPARE(store.state(), ModelStore::State::Failed);
        QVERIFY(store.error().contains(QLatin1String("HTTPS")));
        // Loopback http without the test switch is refused too.
        ModelStoreOptions o2 = options("http://127.0.0.1:1/x.zip", makePackage().manifest);
        o2.allowLoopbackHttp = false;
        ModelStore store2(dir.path(), o2);
        QTRY_VERIFY(!store2.checking());
        store2.download();
        QCOMPARE(store2.state(), ModelStore::State::Failed);
    }

    void cancelDownload() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest& r) {
            TestResponse resp = serveRanges(p.zip, r);
            resp.chunkDelayMs = 40;
            return resp;
        });
        QTemporaryDir dir;
        ModelStore store(dir.path(), options(server.url("/asset.zip"), p.manifest));
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_VERIFY_WITH_TIMEOUT(store.bytesDone() > 0, 10000);
        store.cancel();
        QCOMPARE(store.state(), ModelStore::State::Missing);
        QTRY_VERIFY(!store.checking());
        QTest::qWait(300);
        QCOMPARE(store.state(), ModelStore::State::Missing);
        QVERIFY(store.error().isEmpty());
        const qint64 kept = QFileInfo(store.modelRoot() + "/.download/test-models.zip.part").size();
        QVERIFY(kept > 0 && kept < p.manifest.zipSize);
        // Resume after cancel.
        server.setHandler([&](const TestRequest& r) { return serveRanges(p.zip, r); });
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 10000);
        QVERIFY(server.requests().last().headers.value("range").startsWith("bytes="));
    }

    void stallTimeout() {
        const Package p = makePackage();
        TestHttpServer server([&](const TestRequest&) { return TestResponse{200, {}, {}, -1, true}; });
        QTemporaryDir dir;
        ModelStoreOptions o = options(server.url("/asset.zip"), p.manifest);
        o.stallTimeoutMs = 300;
        ModelStore store(dir.path(), o);
        QTRY_VERIFY(!store.checking());
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Failed, 5000);
    }

    void developerOverride() {
        const Package p = makePackage();
        QTemporaryDir models, data;
        for (const ZipItem& it : p.items) {
            const QString path = models.path() + "/" + QString::fromLatin1(it.name);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.data);
        }
        qputenv("CRISPPLAY_SUBS_MODEL_ROOT", QDir::toNativeSeparators(models.path()).toLocal8Bit());
        {
            ModelStore store(data.path(), options("http://127.0.0.1:1/never", p.manifest));
            QVERIFY(store.usesDeveloperOverride());
            QCOMPARE(store.modelRoot(), QDir::cleanPath(models.path()));
            QTRY_VERIFY(!store.checking());
            QCOMPARE(store.state(), ModelStore::State::Ready);
            QVERIFY(!QFileInfo::exists(models.path() + "/.crispplay-models.json")); // nothing written there
        }
        QFile::remove(models.path() + "/" + QString::fromLatin1(kOcrPath));
        {
            ModelStore store(data.path(), options("http://127.0.0.1:1/never", p.manifest));
            QTRY_VERIFY(!store.checking());
            QCOMPARE(store.state(), ModelStore::State::Missing);
            store.download(); // never downloads into a developer folder
            QCOMPARE(store.state(), ModelStore::State::Failed);
        }
        // A folder that does not exist is ignored.
        qputenv("CRISPPLAY_SUBS_MODEL_ROOT", QByteArray("Z:\\no\\such\\folder"));
        {
            ModelStore store(data.path(), options("http://127.0.0.1:1/never", p.manifest));
            QVERIFY(!store.usesDeveloperOverride());
        }
        qunsetenv("CRISPPLAY_SUBS_MODEL_ROOT");
    }

    // The real release asset built by package-subs-models.ps1, installed offline: a complete
    // .part file skips the network, so this checks zip SHA-256, the zip reader on .NET's
    // output and every file against the compiled-in manifest. Skipped without ..\dist.
    void realPackageOffline() {
        const QString zipPath = QStringLiteral(CRISPPLAY_SUBS_MODELS_ZIP);
        if (!QFileInfo::exists(zipPath)) QSKIP("dist/crispplay-subs-models-v1.zip not built");
        QTemporaryDir dir;
        ModelStoreOptions o = options("http://127.0.0.1:1/never", builtInModelManifest()); // nothing listens there
        const QString part = dir.path() + "/models/.download/" + o.manifest.assetName + ".part";
        QVERIFY(QDir().mkpath(QFileInfo(part).absolutePath()));
        QVERIFY(QFile::copy(zipPath, part));
        ModelStore store(dir.path(), o);
        QTRY_VERIFY(!store.checking());
        QCOMPARE(store.state(), ModelStore::State::Missing);
        store.download();
        QTRY_COMPARE_WITH_TIMEOUT(store.state(), ModelStore::State::Ready, 120000);
        QVERIFY(QFileInfo(store.translatorModelDir() + "/model.bin").size() == 237977893);
        QVERIFY(QFileInfo::exists(store.ocrModelPath()));
        QVERIFY(QFileInfo::exists(store.translatorModelDir() + "/LICENSE-CC-BY-4.0.txt"));
        QVERIFY(!QFileInfo::exists(part));
    }

    // --- secrets and config -------------------------------------------------------------------
    void dpapiRoundTrip() {
        QTemporaryDir dir;
        const std::string d = utf8(dir.path() + QStringLiteral("/veri ğüş")); // Unicode path, created on demand
        const std::string key = "abcd-1234-secret:fx";
        QVERIFY(!hasApiKey(d, OnlineProvider::DeepL));
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "  " + key + "\n"));
        QVERIFY(hasApiKey(d, OnlineProvider::DeepL));
        QVERIFY(!hasApiKey(d, OnlineProvider::OpenAICompatible));
        QCOMPARE(online::loadApiKey(d, OnlineProvider::DeepL).value(), key);
        QFile f(online::keyPath(d, OnlineProvider::DeepL));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray blob = f.readAll();
        f.close();
        QVERIFY(!blob.contains("secret"));
        QVERIFY(!blob.contains(QByteArray::fromStdString(key)));
        // Tampered blob -> no key.
        QByteArray bad = blob;
        bad[bad.size() / 2] = char(bad[bad.size() / 2] ^ 0x55);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bad);
        f.close();
        QVERIFY(!hasApiKey(d, OnlineProvider::DeepL));
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, key));
        QVERIFY(hasApiKey(d, OnlineProvider::DeepL));
        clearApiKey(d, OnlineProvider::DeepL);
        QVERIFY(!hasApiKey(d, OnlineProvider::DeepL));
        QVERIFY(saveApiKey(d, OnlineProvider::OpenAICompatible, "sk-1"));
        QVERIFY(saveApiKey(d, OnlineProvider::OpenAICompatible, "   ")); // empty = clear
        QVERIFY(!hasApiKey(d, OnlineProvider::OpenAICompatible));
        QVERIFY(!saveApiKey(d, OnlineProvider::None, "x"));
    }

    void configLoadSave() {
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        OnlineConfig c = loadOnlineConfig(d);
        QCOMPARE(int(c.provider), int(OnlineProvider::None));
        QCOMPARE(c.timeoutMs, 2500);
        QVERIFY(c.endpoint.empty() && c.model.empty());

        c.provider = OnlineProvider::OpenAICompatible;
        c.endpoint = " https://api.example.com/v1 ";
        c.model = "gpt-test";
        c.timeoutMs = 99999;
        QVERIFY(saveOnlineConfig(d, c));
        QVERIFY(saveApiKey(d, OnlineProvider::OpenAICompatible, "sk-supersecret"));
        const OnlineConfig back = loadOnlineConfig(d);
        QCOMPARE(int(back.provider), int(OnlineProvider::OpenAICompatible));
        QCOMPARE(back.endpoint, std::string("https://api.example.com/v1"));
        QCOMPARE(back.model, std::string("gpt-test"));
        QCOMPARE(back.timeoutMs, 30000);
        QFile f(online::configPath(d));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray json = f.readAll();
        QVERIFY(!json.contains("supersecret"));
        QCOMPARE(QJsonDocument::fromJson(json).object().value("provider").toString(), QStringLiteral("openai-compatible"));
        f.close();

        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ not json");
        f.close();
        QCOMPARE(int(loadOnlineConfig(d).provider), int(OnlineProvider::None));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"version":1,"provider":"martian","timeoutMs":10})");
        f.close();
        const OnlineConfig odd = loadOnlineConfig(d);
        QCOMPARE(int(odd.provider), int(OnlineProvider::None));
        QCOMPARE(odd.timeoutMs, 500);
    }

    void factoryRules() {
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        OnlineConfig c;
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "k:fx"));
        QVERIFY(saveApiKey(d, OnlineProvider::OpenAICompatible, "sk"));
        c.provider = OnlineProvider::None;
        QVERIFY(!makeOnlineTranslator(d, c));               // off: never anything
        c.provider = OnlineProvider::DeepL;
        QVERIFY(makeOnlineTranslator(d, c));
        c.endpoint = "http://api.example.com";               // plain http to a remote host
        QVERIFY(!makeOnlineTranslator(d, c));
        c.provider = OnlineProvider::OpenAICompatible;
        c.endpoint = "https://api.example.com/v1";
        c.model = "";
        QVERIFY(!makeOnlineTranslator(d, c));                // no model
        c.model = "m";
        QVERIFY(makeOnlineTranslator(d, c));
        c.endpoint = "";
        QVERIFY(!makeOnlineTranslator(d, c));                // no endpoint
        c.endpoint = "http://localhost:11434/v1";            // local LLM server is fine
        QVERIFY(makeOnlineTranslator(d, c));
        clearApiKey(d, OnlineProvider::OpenAICompatible);
        QVERIFY(!makeOnlineTranslator(d, c));                // no key: nothing
    }

    void deeplHost() {
        OnlineConfig c;
        c.provider = OnlineProvider::DeepL;
        QCOMPARE(online::deeplUrl(c, "abc:fx").toString(), QStringLiteral("https://api-free.deepl.com/v2/translate"));
        QCOMPARE(online::deeplUrl(c, "abc").toString(), QStringLiteral("https://api.deepl.com/v2/translate"));
        c.endpoint = "https://api.deepl.com/";
        QCOMPARE(online::deeplUrl(c, "abc:fx").toString(), QStringLiteral("https://api.deepl.com/v2/translate"));
        c.endpoint = "ftp://x";
        QVERIFY(!online::deeplUrl(c, "abc").isValid());
        OnlineConfig o;
        o.endpoint = "https://api.openai.com/v1/";
        QCOMPARE(online::openAiUrl(o).toString(), QStringLiteral("https://api.openai.com/v1/chat/completions"));
        o.endpoint = "https://x.example/v1/chat/completions";
        QCOMPARE(online::openAiUrl(o).toString(), QStringLiteral("https://x.example/v1/chat/completions"));
    }

    // --- online requests (local server) --------------------------------------------------------
    void deeplRequest() {
        TestHttpServer server([](const TestRequest&) {
            return TestResponse{200, {{"Content-Type", "application/json"}},
                                R"({"translations":[{"detected_source_language":"EN","text":"Merhaba, dünya."}]})"};
        });
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "test-key:fx"));
        OnlineConfig c;
        c.provider = OnlineProvider::DeepL;
        c.endpoint = server.url("").toStdString();
        auto t = makeOnlineTranslator(d, c);
        QVERIFY(t);
        QCOMPARE(t->translate("Hello, world.").value(), std::string("Merhaba, dünya."));
        QCOMPARE(t->translate("   ").value(), std::string()); // blank: nothing sent
        QCOMPARE(server.requests().size(), 1);
        const TestRequest r = server.requests().first();
        QCOMPARE(r.method, QByteArray("POST"));
        QCOMPARE(r.path, QByteArray("/v2/translate"));
        QCOMPARE(r.headers.value("authorization"), QByteArray("DeepL-Auth-Key test-key:fx"));
        QVERIFY(r.headers.value("content-type").startsWith("application/json"));
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        QCOMPARE(body.value("text").toArray(), QJsonArray{QStringLiteral("Hello, world.")});
        QCOMPARE(body.value("source_lang").toString(), QStringLiteral("EN"));
        QCOMPARE(body.value("target_lang").toString(), QStringLiteral("TR"));
        // Too long input is never sent.
        QVERIFY(!t->translate(std::string(online::kMaxInputBytes + 1, 'a')));
        QCOMPARE(server.requests().size(), 1);
    }

    void deeplErrors() {
        QByteArray reply;
        int status = 200;
        TestHttpServer server([&](const TestRequest&) { return TestResponse{status, {}, reply}; });
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "k"));
        OnlineConfig c;
        c.provider = OnlineProvider::DeepL;
        c.endpoint = server.url("/v2/translate").toStdString();
        auto t = makeOnlineTranslator(d, c);
        QVERIFY(t);
        status = 403; reply = R"({"message":"Forbidden"})";
        QVERIFY(!t->translate("Hi"));
        status = 456; reply = "{}";
        QVERIFY(!t->translate("Hi"));
        status = 200; reply = "not json";
        QVERIFY(!t->translate("Hi"));
        reply = R"({"translations":[]})";
        QVERIFY(!t->translate("Hi"));
        reply = R"({"translations":[{"text":"Selam"}]})";
        QCOMPARE(t->translate("Hi").value(), std::string("Selam"));
        // Redirects are not followed (the key must not travel elsewhere).
        status = 302; reply = {};
        QVERIFY(!t->translate("Hi"));
    }

    void openAiRequest() {
        QByteArray reply = R"({"choices":[{"index":0,"finish_reason":"stop","message":{"role":"assistant","content":" Seni bekliyordum. \n"}}]})";
        TestHttpServer server([&](const TestRequest&) { return TestResponse{200, {{"Content-Type", "application/json"}}, reply}; });
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        QVERIFY(saveApiKey(d, OnlineProvider::OpenAICompatible, "sk-test"));
        OnlineConfig c;
        c.provider = OnlineProvider::OpenAICompatible;
        c.endpoint = server.url("/v1/").toStdString();
        c.model = "small-model";
        auto t = makeOnlineTranslator(d, c);
        QVERIFY(t);
        QCOMPARE(t->translate("I was waiting for you.").value(), std::string("Seni bekliyordum."));
        const TestRequest r = server.requests().first();
        QCOMPARE(r.path, QByteArray("/v1/chat/completions"));
        QCOMPARE(r.headers.value("authorization"), QByteArray("Bearer sk-test"));
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        QCOMPARE(body.value("model").toString(), QStringLiteral("small-model"));
        QCOMPARE(body.value("temperature").toDouble(-1), 0.0);
        const int maxTokens = body.value("max_tokens").toInt();
        QVERIFY(maxTokens >= 96 && maxTokens <= 512);
        const QJsonArray messages = body.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject().value("role").toString(), QStringLiteral("system"));
        const QString prompt = messages[0].toObject().value("content").toString();
        QVERIFY(prompt.contains(QLatin1String("Turkish")) && prompt.contains(QLatin1String("\"sen\"")));
        QCOMPARE(messages[1].toObject().value("role").toString(), QStringLiteral("user"));
        QCOMPARE(messages[1].toObject().value("content").toString(), QStringLiteral("I was waiting for you."));

        reply = R"({"choices":[{"finish_reason":"length","message":{"content":"Seni bekli"}}]})";
        QVERIFY(!t->translate("I was waiting for you."));
        reply = R"({"choices":[]})";
        QVERIFY(!t->translate("x"));
        reply = R"({"choices":[{"message":{"content":""}}]})";
        QVERIFY(!t->translate("x"));
        QCOMPARE(online::openAiMaxTokens(std::string(5000, 'a')), 512);
        QCOMPARE(online::openAiMaxTokens("hi"), 96);
    }

    void timeoutGivesNullopt() {
        TestHttpServer server([](const TestRequest&) { return TestResponse{200, {}, {}, -1, true}; });
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "k"));
        OnlineConfig c;
        c.provider = OnlineProvider::DeepL;
        c.endpoint = server.url("").toStdString();
        c.timeoutMs = 500;
        auto t = makeOnlineTranslator(d, c);
        QVERIFY(t);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!t->translate("Hello"));
        QVERIFY2(timer.elapsed() < 2500, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(timer.elapsed() >= 400);
    }

    void cancelThenWorksAgain() {
        bool hang = true;
        TestHttpServer server([&](const TestRequest&) {
            TestResponse r{200, {}, R"({"translations":[{"text":"Tamam"}]})"};
            r.hang = hang;
            return r;
        });
        QTemporaryDir dir;
        const std::string d = utf8(dir.path());
        QVERIFY(saveApiKey(d, OnlineProvider::DeepL, "k"));
        OnlineConfig c;
        c.provider = OnlineProvider::DeepL;
        c.endpoint = server.url("").toStdString();
        c.timeoutMs = 10000;
        auto t = makeOnlineTranslator(d, c);
        QVERIFY(t);
        std::thread canceller([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            t->cancel();
        });
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!t->translate("Hello"));
        canceller.join();
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
        hang = false;
        QCOMPARE(t->translate("OK").value(), std::string("Tamam"));
    }
};

QTEST_MAIN(NetTests)
#include "NetTests.moc"
