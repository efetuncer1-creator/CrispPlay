#include "crispplay/discord/IpcClient.h"
#include "crispplay/discord/Presence.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTest>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace crispplay::discord;

namespace {

PresenceInput input(PrivacyLevel level, SourceType source, bool sr = false, bool nr = false, bool fg = false) {
    PresenceInput in;
    in.level = level;
    in.source = source;
    in.superResolution = sr;
    in.noiseReduction = nr;
    in.frameGeneration = fg;
    in.startedAtUnix = 1700000000;
    return in;
}

QString asText(const QJsonObject& o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }

// Blocking frame reader for the fake Discord side of the pipe.
bool readExact(HANDLE h, char* dst, DWORD n) {
    DWORD got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, dst + got, n - got, &r, nullptr) || r == 0) return false;
        got += r;
    }
    return true;
}
bool readFrame(HANDLE h, Frame& f) {
    char head[8];
    if (!readExact(h, head, 8)) return false;
    quint32 op = 0, len = 0;
    memcpy(&op, head, 4);        // little-endian host
    memcpy(&len, head + 4, 4);
    QByteArray body(int(len), 0);
    if (len && !readExact(h, body.data(), len)) return false;
    f.opcode = op;
    f.payload = body;
    return true;
}

} // namespace

class DiscordTests final : public QObject {
    Q_OBJECT
private slots:
    // --- payload ---------------------------------------------------------------
    void offAndIdleClear() {
        QVERIFY(!buildActivity(input(PrivacyLevel::Off, SourceType::File)).has_value());
        QVERIFY(!buildActivity(input(PrivacyLevel::Basic, SourceType::None)).has_value());
        QVERIFY(!buildActivity(input(PrivacyLevel::Detailed, SourceType::None)).has_value());
    }

    void basicHasOnlyBrandAndTimer() {
        const auto a = buildActivity(input(PrivacyLevel::Basic, SourceType::Capture, true, true, true));
        QVERIFY(a);
        QCOMPARE(a->value(QStringLiteral("details")).toString(), QStringLiteral("CrispPlay ile izliyor"));
        QVERIFY(!a->contains(QStringLiteral("state")));   // no source type, no enhancements
        QCOMPARE(a->value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger(), 1700000000);
        const QString text = asText(*a);
        QVERIFY(!text.contains(QStringLiteral("Yakalama")));
        QVERIFY(!text.contains(QStringLiteral("SR")));
    }

    void detailedAddsSourceTypeAndEnhancements() {
        const struct { SourceType t; const char* label; } types[] = {
            {SourceType::File, "Video dosyası"}, {SourceType::Capture, "Yakalama kartı"},
            {SourceType::Screen, "Ekran yakalama"}, {SourceType::Ps5, "PS5 yayını"},
            {SourceType::Xbox, "Xbox yayını"}, {SourceType::Pc, "PC yayını"},
        };
        for (const auto& c : types) {
            const auto a = buildActivity(input(PrivacyLevel::Detailed, c.t));
            QVERIFY(a);
            QCOMPARE(a->value(QStringLiteral("state")).toString(), QString::fromUtf8(c.label));
        }
        const auto a = buildActivity(input(PrivacyLevel::Detailed, SourceType::File, true, true, true));
        QCOMPARE(a->value(QStringLiteral("state")).toString(), QString::fromUtf8("Video dosyası · SR + NR + FG"));
        const auto b = buildActivity(input(PrivacyLevel::Detailed, SourceType::Ps5, false, true, false));
        QCOMPARE(b->value(QStringLiteral("state")).toString(), QString::fromUtf8("PS5 yayını · NR"));
    }

    // The input struct cannot hold text. The only way text enters is the bridge's
    // source-kind string, which is mapped to an enum; a file name there maps to nothing.
    void filenameCannotLeak() {
        const QString evil = QStringLiteral("C:\\Users\\efe\\Videos\\gizli film (2024).mkv");
        QCOMPARE(sourceTypeFromBridgeKind(evil), SourceType::None);
        QCOMPARE(sourceTypeFromBridgeKind(QStringLiteral("file ") + evil), SourceType::None);
        QVERIFY(!buildActivity(input(PrivacyLevel::Detailed, sourceTypeFromBridgeKind(evil))).has_value());

        const QStringList canaries{QStringLiteral("gizli"), QStringLiteral("mkv"), QStringLiteral("Users"),
                                   QStringLiteral("C:\\"), QStringLiteral("192.168"), QStringLiteral("efe")};
        const QStringList allowedKeys{QStringLiteral("type"), QStringLiteral("details"), QStringLiteral("state"),
                                      QStringLiteral("timestamps"), QStringLiteral("assets")};
        const SourceType sources[] = {SourceType::None, SourceType::File, SourceType::Image, SourceType::Capture,
                                      SourceType::Screen, SourceType::Ps5, SourceType::Xbox, SourceType::Pc,
                                      SourceType::Network};
        for (auto level : {PrivacyLevel::Off, PrivacyLevel::Basic, PrivacyLevel::Detailed})
            for (auto s : sources)
                for (int mask = 0; mask < 8; ++mask) {
                    const auto a = buildActivity(input(level, s, mask & 1, mask & 2, mask & 4));
                    if (!a) continue;
                    for (const QString& key : a->keys()) QVERIFY2(allowedKeys.contains(key), qPrintable(key));
                    const QString text = asText(*a);
                    for (const QString& c : canaries) QVERIFY2(!text.contains(c), qPrintable(text));
                }
    }

    void levelStrings() {
        for (auto l : {PrivacyLevel::Off, PrivacyLevel::Basic, PrivacyLevel::Detailed})
            QCOMPARE(levelFromString(levelToString(l)), l);
        QCOMPARE(levelFromString(QStringLiteral("garbage")), PrivacyLevel::Off);
        QCOMPARE(levelFromString(QString()), PrivacyLevel::Off);
    }

    void clientIdValidation() {
        QVERIFY(!isValidClientId(QString()));
        QVERIFY(!isValidClientId(QStringLiteral("abc")));
        QVERIFY(!isValidClientId(QStringLiteral("12345 6789")));
        QVERIFY(isValidClientId(QStringLiteral("123456789012345678")));
    }

    // --- wire format -----------------------------------------------------------
    void frameEncoding() {
        const QByteArray f = encodeFrame(OpHandshake, QByteArray("{}"));
        QCOMPARE(f, QByteArray("\x00\x00\x00\x00\x02\x00\x00\x00{}", 10));
        const QByteArray g = encodeFrame(OpFrame, QByteArray(300, 'x'));
        QCOMPARE(quint8(g[0]), quint8(1));
        QCOMPARE(quint8(g[4]), quint8(0x2C));   // 300 = 0x012C, little-endian
        QCOMPARE(quint8(g[5]), quint8(0x01));
        QCOMPARE(g.size(), 308);
    }

    void handshakeAndSetActivityPayloads() {
        const auto h = QJsonDocument::fromJson(buildHandshake(QStringLiteral("123456789012345678"))).object();
        QCOMPARE(h.value(QStringLiteral("v")).toInt(), 1);
        QCOMPARE(h.value(QStringLiteral("client_id")).toString(), QStringLiteral("123456789012345678"));

        const auto act = buildActivity(input(PrivacyLevel::Basic, SourceType::File));
        const auto set = QJsonDocument::fromJson(buildSetActivity(4242, act, QStringLiteral("n-1"))).object();
        QCOMPARE(set.value(QStringLiteral("cmd")).toString(), QStringLiteral("SET_ACTIVITY"));
        QCOMPARE(set.value(QStringLiteral("nonce")).toString(), QStringLiteral("n-1"));
        QCOMPARE(set.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toInt(), 4242);
        QVERIFY(set.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).isObject());

        const auto clear = QJsonDocument::fromJson(buildSetActivity(4242, std::nullopt, QStringLiteral("n-2"))).object();
        QVERIFY(clear.value(QStringLiteral("args")).toObject().contains(QStringLiteral("activity")));
        QVERIFY(clear.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).isNull());
    }

    void parserReassemblesAndRejectsOversize() {
        const QByteArray a = encodeFrame(OpFrame, QByteArray("{\"a\":1}"));
        const QByteArray b = encodeFrame(OpPing, QByteArray("{}"));
        const QByteArray all = a + b;
        FrameParser p;
        Frame f;
        for (int i = 0; i < all.size(); ++i) {   // one byte at a time
            p.append(all.mid(i, 1));
            if (i < a.size() - 1) QVERIFY(!p.next(f));
        }
        QVERIFY(p.next(f));
        QCOMPARE(f.opcode, quint32(OpFrame));
        QCOMPARE(f.payload, QByteArray("{\"a\":1}"));
        QVERIFY(p.next(f));
        QCOMPARE(f.opcode, quint32(OpPing));
        QVERIFY(!p.next(f));

        FrameParser bad;
        QByteArray header(8, 0);
        header[7] = char(0x7F);   // length ~2 GB
        bad.append(header);
        QVERIFY(!bad.next(f));
        QVERIFY(bad.failed());
    }

    // --- pipe behaviour --------------------------------------------------------
    void absentPipeDoesNotHang() {
        IpcClient::Options o;
        o.clientId = QStringLiteral("123456789012345678");
        o.pipeBase = QStringLiteral("\\\\.\\pipe\\crispplay-test-absent-%1-").arg(GetCurrentProcessId());
        QElapsedTimer total;
        total.start();
        {
            IpcClient client(o);
            QElapsedTimer t;
            t.start();
            client.setActivity(buildActivity(input(PrivacyLevel::Detailed, SourceType::File, true)));
            QVERIFY2(t.elapsed() < 100, "setActivity must return immediately");
            // Worker searches for the pipe (state Searching) without ever blocking us.
            QTRY_VERIFY_WITH_TIMEOUT(client.state() == IpcClient::State::Searching, 2000);
            QElapsedTimer s;
            s.start();
            client.shutdown();
            QVERIFY2(s.elapsed() < 1000, "shutdown must not wait for the backoff");
            QCOMPARE(client.state(), IpcClient::State::Idle);
        }
        QVERIFY2(total.elapsed() < 4000, qPrintable(QString::number(total.elapsed())));
    }

    void idleClientNeverTouchesThePipe() {
        IpcClient::Options o;
        o.clientId = QStringLiteral("123456789012345678");
        o.pipeBase = QStringLiteral("\\\\.\\pipe\\crispplay-test-idle-%1-").arg(GetCurrentProcessId());
        IpcClient client(o);
        QTest::qWait(300);
        QCOMPARE(client.state(), IpcClient::State::Idle);   // nothing to show: no searching either
        client.setActivity(std::nullopt);
        QTest::qWait(100);
        QCOMPARE(client.state(), IpcClient::State::Idle);
    }

    // A fake Discord on a private pipe name: handshake, READY, SET_ACTIVITY, clear on shutdown.
    void talksToFakeDiscord() {
        const QString base = QStringLiteral("\\\\.\\pipe\\crispplay-test-fake-%1-").arg(GetCurrentProcessId());
        const QString name = base + QStringLiteral("0");
        HANDLE server = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(name.utf16()), PIPE_ACCESS_DUPLEX,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
        QVERIFY(server != INVALID_HANDLE_VALUE);

        std::mutex m;
        std::vector<Frame> received;
        std::thread srv([&] {
            if (!ConnectNamedPipe(server, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
            Frame f;
            if (!readFrame(server, f)) return;
            { std::lock_guard l(m); received.push_back(f); }
            const QByteArray ready = encodeFrame(OpFrame, QByteArray("{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{}}"));
            DWORD w = 0;
            WriteFile(server, ready.constData(), DWORD(ready.size()), &w, nullptr);
            while (readFrame(server, f)) { std::lock_guard l(m); received.push_back(f); }
        });
        const auto count = [&] { std::lock_guard l(m); return received.size(); };

        IpcClient::Options o;
        o.clientId = QStringLiteral("123456789012345678");
        o.pipeBase = base;
        o.minUpdateIntervalMs = 0;
        {
            IpcClient client(o);
            client.setActivity(buildActivity(input(PrivacyLevel::Detailed, SourceType::Ps5, true, false, true)));
            QTRY_VERIFY_WITH_TIMEOUT(count() >= 2, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(client.state(), IpcClient::State::Connected, 2000);
            client.shutdown();   // sends the clear before closing
        }
        srv.join();
        CloseHandle(server);

        QCOMPARE(received.size(), size_t(3));
        QCOMPARE(received[0].opcode, quint32(OpHandshake));
        const auto hs = QJsonDocument::fromJson(received[0].payload).object();
        QCOMPARE(hs.value(QStringLiteral("client_id")).toString(), QStringLiteral("123456789012345678"));
        QCOMPARE(hs.value(QStringLiteral("v")).toInt(), 1);

        QCOMPARE(received[1].opcode, quint32(OpFrame));
        const auto set = QJsonDocument::fromJson(received[1].payload).object();
        QCOMPARE(set.value(QStringLiteral("cmd")).toString(), QStringLiteral("SET_ACTIVITY"));
        QCOMPARE(set.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toInt(), int(GetCurrentProcessId()));
        const auto activity = set.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        QCOMPARE(activity.value(QStringLiteral("state")).toString(), QString::fromUtf8("PS5 yayını · SR + FG"));

        const auto clear = QJsonDocument::fromJson(received[2].payload).object();
        QVERIFY(clear.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).isNull());
    }
};

QTEST_GUILESS_MAIN(DiscordTests)
#include "DiscordTests.moc"
