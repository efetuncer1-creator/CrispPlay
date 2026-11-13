// Unit tests of the subtitle translation engine (custom/subs/mt).
// Text helpers always run; translator tests need CTranslate2 (CRISPPLAY_HAVE_CT2)
// and the model tests additionally the OPUS-MT model (CRISPPLAY_MT_MODEL or the
// CMake default path), otherwise they are skipped.

#include <crispplay/subs/mt/TextSplit.h>
#ifdef CRISPPLAY_HAVE_CT2
#include <crispplay/subs/mt/OpusMtTranslator.h>
#endif

#include <QtTest>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace crispplay::subs::mt;
using Parts = std::vector<std::string>;

namespace {

#ifdef CRISPPLAY_HAVE_CT2
std::filesystem::path modelDir()
{
    const QByteArray env = qgetenv("CRISPPLAY_MT_MODEL");
    if (!env.isEmpty()) return std::filesystem::path(QString::fromUtf8(env).toStdWString());
    return std::filesystem::path(QString::fromUtf8(CRISPPLAY_MT_DEFAULT_MODEL).toStdWString());
}

bool haveModel()
{
    std::error_code ec;
    return std::filesystem::is_regular_file(modelDir() / "model.bin", ec);
}
#endif

QString q(const std::string& s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

} // namespace

class MtTests : public QObject {
    Q_OBJECT

private slots:
    void splitMatchesSpike_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("single") << "Almost." << QStringList{"Almost."};
        QTest::newRow("two") << "Pick it up later. These crates need unloading."
                             << QStringList{"Pick it up later.", "These crates need unloading."};
        QTest::newRow("question+bang") << "Romano, you done with your count? Delivery's here!"
                                       << QStringList{"Romano, you done with your count?", "Delivery's here!"};
        QTest::newRow("abbreviation") << "Dr. Kovacs wants to see you in the lab right away."
                                      << QStringList{"Dr. Kovacs wants to see you in the lab right away."};
        QTest::newRow("lowercase-next") << "Wait... what?" << QStringList{"Wait... what?"};
        QTest::newRow("ellipsis") << QString::fromUtf8("Strùnzu\u2026 Get out.")
                                  << QStringList{QString::fromUtf8("Strùnzu\u2026"), "Get out."};
        QTest::newRow("quote-close") << "He said \"Run.\" Then he left."
                                     << QStringList{"He said \"Run.\"", "Then he left."};
        QTest::newRow("quote-open") << "Listen. \"Nobody moves.\""
                                    << QStringList{"Listen.", "\"Nobody moves.\""};
        QTest::newRow("digit") << "Wave incoming. 3 left." << QStringList{"Wave incoming.", "3 left."};
        QTest::newRow("no-space") << "Version 2.5 is out.Now." << QStringList{"Version 2.5 is out.Now."};
        QTest::newRow("spaces") << "  Reloading!   Cover me!  " << QStringList{"Reloading!", "Cover me!"};
        QTest::newRow("newline") << "Go.\nNow!" << QStringList{"Go.", "Now!"};
        QTest::newRow("punct-run") << "What?! No way." << QStringList{"What?!", "No way."};
    }

    void splitMatchesSpike()
    {
        QFETCH(QString, input);
        QFETCH(QStringList, expected);
        const QByteArray utf8 = input.toUtf8();
        QStringList got;
        for (const auto& p : splitSentences(std::string_view(utf8.constData(), size_t(utf8.size())))) got << q(p);
        QCOMPARE(got, expected);
    }

    void joinUsesOneSpace()
    {
        QCOMPARE(q(joinSentences(Parts{"Eğilin!", "Bizi soldan kuşatıyorlar!"})), QString::fromUtf8("Eğilin! Bizi soldan kuşatıyorlar!"));
        QCOMPARE(q(joinSentences(Parts{"Tek."})), QStringLiteral("Tek."));
        QCOMPARE(q(joinSentences(Parts{})), QString());
    }

    void emptyAndWhitespace()
    {
        QVERIFY(splitSentences("").empty());
        QVERIFY(splitSentences(" \t\r\n ").empty());
        QCOMPARE(q(std::string(trim("  a b \n"))), QStringLiteral("a b"));
    }

    void truncateKeepsUtf8AndWords()
    {
        QCOMPARE(q(truncateUtf8("short", 100)), QStringLiteral("short"));
        // Cut inside "ş" (2 bytes) must not leave half a code point.
        const std::string s = "aaaaaaaaaş";
        const std::string t = truncateUtf8(s, 10);
        QCOMPARE(t.size(), size_t(9));
        // Prefers a word boundary near the end.
        QCOMPARE(q(truncateUtf8("one two three four", 16)), QStringLiteral("one two three"));
        // Long input is bounded.
        const std::string longLine(5000, 'x');
        QCOMPARE(truncateUtf8(longLine, 1000).size(), size_t(1000));
    }

    void unknownMarker()
    {
        QVERIFY(containsUnknownMarker("Str \xE2\x81\x87 nzu..."));
        QVERIFY(!containsUnknownMarker("Neredeyse."));
        QVERIFY(!containsUnknownMarker("?? <unk>"));
    }

    // The translator slots exist in every build (moc does not see compile
    // definitions reliably); without CTranslate2 they skip.
    void missingModelReturnsNullopt()
    {
#ifndef CRISPPLAY_HAVE_CT2
        QSKIP("built without CTranslate2");
#else
        OpusMtTranslator t(std::filesystem::path(L"Z:/does-not-exist/ünïcode-model"));
        QVERIFY(!t.translate("Hello there.").has_value());
        QVERIFY(!t.isLoaded());
        QVERIFY(!t.lastError().empty());
        QVERIFY(!t.translate("Again.").has_value()); // failure is remembered, still no crash
        t.cancel();
#endif
    }

    void brokenModelReturnsNullopt()
    {
#ifndef CRISPPLAY_HAVE_CT2
        QSKIP("built without CTranslate2");
#else
        const auto dir = std::filesystem::temp_directory_path() / "crispplay-mt-broken-model";
        std::filesystem::create_directories(dir);
        for (const char* name : {"model.bin", "config.json", "source.spm", "target.spm"}) {
            FILE* f = nullptr;
            _wfopen_s(&f, (dir / name).c_str(), L"wb");
            QVERIFY(f);
            fputs("garbage", f);
            fclose(f);
        }
        OpusMtTranslator t(dir);
        QVERIFY(!t.translate("Hello there.").has_value());
        QVERIFY(!t.lastError().empty());
        std::filesystem::remove_all(dir);
#endif
    }

    void translatesWithModel()
    {
#ifndef CRISPPLAY_HAVE_CT2
        QSKIP("built without CTranslate2");
#else
        if (!haveModel()) QSKIP("OPUS-MT model not found (set CRISPPLAY_MT_MODEL)");
        OpusMtTranslator t(modelDir());
        QVERIFY(t.load());
        QVERIFY(t.loadTimeMs() > 0);

        const auto empty = t.translate("   ");
        QVERIFY(empty.has_value());
        QVERIFY(empty->empty());

        const auto two = t.translate("Pick it up later. These crates need unloading.");
        QVERIFY(two.has_value());
        QCOMPARE(q(*two), QString::fromUtf8("Sonra alırsın. Bu sandıkların boşaltılması gerekiyor."));
        QVERIFY(t.lastLatencyMs() > 0);
        QVERIFY(!t.lastHadUnknown());

        const auto unk = t.translate("Strùnzu...");
        QVERIFY(unk.has_value());
        QVERIFY(containsUnknownMarker(*unk)); // kept for the core pipeline to decide
        QVERIFY(t.lastHadUnknown());

        // Very long input is cut, still translated, and stays bounded in time.
        std::string longLine;
        for (int i = 0; i < 200; ++i) longLine += "We need to move the crates before the boss gets back. ";
        const auto lng = t.translate(longLine);
        QVERIFY(lng.has_value());
        QVERIFY(!lng->empty());
        QVERIFY2(t.lastLatencyMs() < 20000, "long input took too long");
#endif
    }

    void cancelStopsRunningTranslation()
    {
#ifndef CRISPPLAY_HAVE_CT2
        QSKIP("built without CTranslate2");
#else
        if (!haveModel()) QSKIP("OPUS-MT model not found (set CRISPPLAY_MT_MODEL)");
        OpusMtTranslator t(modelDir()); // not loaded yet: the call below spends ~0.3 s loading
        std::optional<std::string> result = std::string("unset");
        std::thread worker([&] { result = t.translate("Hold the line! Don't let them through! Fall back to the bridge."); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        t.cancel();
        worker.join();
        QVERIFY(!result.has_value());
        // The next call is not affected.
        const auto again = t.translate("Almost.");
        QVERIFY(again.has_value());
        QCOMPARE(q(*again), QStringLiteral("Neredeyse."));
#endif
    }
};

QTEST_GUILESS_MAIN(MtTests)
#include "MtTests.moc"
