#include "crispplay/Branding.h"
#include <QTest>

using namespace crispplay::branding;

class BrandingTests final : public QObject {
    Q_OBJECT
private slots:
    void remoteTags() {
        QCOMPARE(releaseVersion(QStringLiteral("v2.1.6"), false), QVersionNumber({2, 1, 6, 0}));
        QCOMPARE(releaseVersion(QStringLiteral("v2.1.6-crispplay.3"), false), QVersionNumber({2, 1, 6, 3}));
        QVERIFY(releaseVersion(QStringLiteral("v2.1.6-beta"), false).isNull());
        QVERIFY(releaseVersion(QStringLiteral("v2.1.6-sources"), false).isNull());
        QVERIFY(releaseVersion(QStringLiteral("v2.0.2147483648"), false).isNull());
    }
    void localLabels() {
        QCOMPARE(releaseVersion(QStringLiteral("2.1.5-crispplay.1"), true), QVersionNumber({2, 1, 5, 1}));
        QCOMPARE(releaseVersion(QStringLiteral("2.1.5"), true), QVersionNumber({2, 1, 5, 0}));
        QCOMPARE(releaseVersion(QStringLiteral("2.1.5-test-r2"), true), QVersionNumber({2, 1, 5, 0}));
    }
    void ordering() {
        const auto v = [](const char* s) { return releaseVersion(QString::fromLatin1(s), false); };
        QVERIFY(v("v2.1.5-crispplay.2") > v("v2.1.5-crispplay.1"));
        QVERIFY(v("v2.1.6-crispplay.1") > v("v2.1.5-crispplay.9"));
        QVERIFY(v("v2.1.5-crispplay.10") > v("v2.1.5-crispplay.9"));
    }
    void display() {
        QCOMPARE(displayVersion(QVersionNumber({2, 1, 6, 3})), QStringLiteral("2.1.6-crispplay.3"));
        QCOMPARE(displayVersion(QVersionNumber({2, 1, 6, 0})), QStringLiteral("2.1.6"));
    }
    void releaseUrls() {
        const QString tag = QStringLiteral("v2.1.6-crispplay.1");
        QVERIFY(isReleaseUrl(QUrl(QStringLiteral("https://github.com/efetuncer1-creator/CrispPlay/releases/tag/") + tag), tag));
        QVERIFY(!isReleaseUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/releases/tag/") + tag), tag));
        QVERIFY(!isReleaseUrl(QUrl(QStringLiteral("https://github.com/efetuncer1-creator/CrispPlay/releases/tag/v9.9.9")), tag));
    }
};

QTEST_GUILESS_MAIN(BrandingTests)
#include "BrandingTests.moc"
