// Turkish interface language through upstream's loader (src/ui/UiLanguage.cpp)
// with the real upstream catalog and custom/i18n/tr.json compiled in.
#include "crispplay/I18n.h"
#include "veyra/ui/UiLanguage.h"

#include <QCoreApplication>
#include <QTest>

using namespace veyra::ui;

class I18nTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(i18n::apply(QStringLiteral("tr"))); }
    void cleanupTestCase() { i18n::apply(QStringLiteral("zh-CN")); }

    void resolveKeepsTurkish() {
        QCOMPARE(i18n::resolve(QStringLiteral("tr")), QStringLiteral("tr"));
        QCOMPARE(i18n::resolve(QStringLiteral("en")), QStringLiteral("en"));
        QCOMPARE(crispplay::i18n::systemLanguage(QLocale(QLocale::Turkish, QLocale::Turkey)), QStringLiteral("tr"));
        QVERIFY(crispplay::i18n::systemLanguage(QLocale(QLocale::German)).isEmpty());
        QCOMPARE(i18n::current(), QStringLiteral("tr"));
    }
    void exactEntries() {
        QCOMPARE(QCoreApplication::translate("Any", "导出完成"), QStringLiteral(u"Dışa aktarma tamamlandı"));
        QCOMPARE(QCoreApplication::translate("Any", "界面语言"), QStringLiteral(u"Arayüz dili"));
        QCOMPARE(i18n::text(QStringLiteral("导出完成")), QStringLiteral(u"Dışa aktarma tamamlandı"));
    }
    void contextEntry() {
        // 关闭 is "off" as a setting and "close" for a window.
        QCOMPARE(QCoreApplication::translate("Any", "关闭", "off"), QStringLiteral("Kapalı"));
        QCOMPARE(QCoreApplication::translate("Any", "关闭"), QStringLiteral("Kapat"));
    }
    void formatPattern() {
        QCOMPARE(i18n::text(QStringLiteral("主机搜索发生错误（5），详情见日志；可手填 IP。")),
                 QStringLiteral(u"Ana bilgisayar aramasında hata oluştu (5), ayrıntılar günlükte. IP'yi elle girebilirsiniz."));
    }
    void segmentsUseLatinSeparator() {
        QCOMPARE(i18n::text(QStringLiteral("HDR 视频自动使用 HEVC Main10 编码；视频导出完成")),
                 QStringLiteral(u"HDR video otomatik olarak HEVC Main10 kullanır; Video dışa aktarma tamamlandı"));
    }
    void unknownTextUnchanged() {
        QCOMPARE(i18n::text(QStringLiteral("D:/视频/我的片子.mkv")), QStringLiteral("D:/视频/我的片子.mkv"));
    }
    void menuEntry() {
        const auto langs = crispplay::i18n::qmlLanguages();
        QCOMPARE(langs.size(), 1);
        QCOMPARE(langs.front().toMap().value(QStringLiteral("label")).toString(), QStringLiteral(u"T\u00fcrk\u00e7e"));
    }
};

QTEST_GUILESS_MAIN(I18nTests)
#include "I18nTests.moc"
