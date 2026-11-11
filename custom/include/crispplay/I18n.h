// CrispPlay interface languages on top of upstream's catalog (src/ui/UiLanguage.cpp).
// Header-only on purpose: UiLanguage.cpp includes it through short VEYRA_CUSTOM
// hooks, and veyra_ui_i18n does not have to link anything new.
//
// Our languages live in their own files (custom/i18n/<code>.json, compiled in as
// :/crispplay/i18n/<code>.json) with the same entry shape as upstream:
//   {"zh": source, "ctx": optional disambiguation, "en": reference, "<code>": text}
// For such a code the upstream catalog is rewritten in memory so every entry gets a
// "<code>" column: ours where translated, English otherwise (a string added upstream
// and not yet translated shows English instead of Chinese). Upstream's loader then
// runs unchanged, with the same exact / ctx / "{}" pattern / "…：" prefix logic.
#pragma once

#include <QByteArray>
#include <QChar>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace crispplay::i18n {

// Language codes we add to upstream's zh-CN / zh-TW / en / ja.
inline QStringList extraLanguages() { return {QStringLiteral("tr")}; }

inline QString resourcePath(const QString& code) {
    return QStringLiteral(":/crispplay/i18n/") + code + QStringLiteral(".json");
}

// Windows display language -> one of our codes, or empty to let upstream decide.
inline QString systemLanguage(const QLocale& system) {
    return system.language() == QLocale::Turkish ? QStringLiteral("tr") : QString();
}

// Extra entries for the language menu (SettingsPage.qml, through crispBrand.languages).
// Each language is named in itself, like upstream's entries.
inline QVariantList qmlLanguages() {
    return {QVariantMap{{QStringLiteral("id"), QStringLiteral("tr")}, {QStringLiteral("label"), QStringLiteral(u"T\u00fcrk\u00e7e")}}};
}

// Languages written in Latin script: full-width separators are replaced when
// messages are joined from pieces.
inline bool latinScript(const QString& code) {
    return code == QStringLiteral("en") || code == QStringLiteral("tr");
}

// Same key the loader builds: source, or source U+0004 disambiguation.
inline QString entryKey(const QJsonObject& e) {
    const QString source = e.value(QStringLiteral("zh")).toString();
    const QString context = e.value(QStringLiteral("ctx")).toString();
    return context.isEmpty() ? source : source + QChar(0x4) + context;
}

// Upstream catalog bytes -> catalog bytes for `code`. Unchanged for upstream's own
// languages, or when our resource for `code` is missing (the loader then finds no
// column and reports the language as unavailable).
inline QByteArray catalogJson(const QString& code, const QByteArray& upstream, const QString& path = {}) {
    if (!extraLanguages().contains(code)) return upstream;
    QFile file(path.isEmpty() ? resourcePath(code) : path);
    if (!file.open(QIODevice::ReadOnly)) return upstream;
    QHash<QString, QString> ours;
    for (const auto value : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("entries")).toArray()) {
        const auto e = value.toObject();
        const QString text = e.value(code).toString();
        if (!text.isEmpty()) ours.insert(entryKey(e), text);
    }
    if (ours.isEmpty()) return upstream;
    auto root = QJsonDocument::fromJson(upstream).object();
    QJsonArray merged;
    for (const auto value : root.value(QStringLiteral("entries")).toArray()) {
        auto e = value.toObject();
        const auto it = ours.constFind(entryKey(e));
        e.insert(code, it != ours.constEnd() ? *it : e.value(QStringLiteral("en")).toString());
        merged.append(e);
    }
    root.insert(QStringLiteral("entries"), merged);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace crispplay::i18n
