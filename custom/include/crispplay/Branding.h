// CrispPlay branding and release policy.
// Header-only on purpose: upstream files include it through short VEYRA_CUSTOM
// hooks, and no upstream target has to link anything new.
#pragma once

#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVersionNumber>

namespace crispplay::branding {

inline QString appName() { return QStringLiteral("CrispPlay"); }
inline QString homeTitle() { return QStringLiteral("CrispPlay"); }

// Where our releases live. Change only here.
inline QString repoOwner() { return QStringLiteral("efetuncer1-creator"); }
inline QString repoName() { return QStringLiteral("CrispPlay"); }
inline QString repoPath() { return QStringLiteral("/") + repoOwner() + QStringLiteral("/") + repoName(); }
inline QUrl repoUrl() { return QUrl(QStringLiteral("https://github.com") + repoPath()); }
inline QUrl readmeUrl() { return QUrl(repoUrl().toString() + QStringLiteral("#readme")); }
inline QUrl issuesUrl() { return QUrl(repoUrl().toString() + QStringLiteral("/issues")); }
inline QUrl releasesUrl() { return QUrl(repoUrl().toString() + QStringLiteral("/releases")); }
inline QUrl releaseApiUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos") + repoPath() + QStringLiteral("/releases/latest"));
}
inline QString releaseTagPath(const QString& tag) { return repoPath() + QStringLiteral("/releases/tag/") + tag; }

// GPL-3 attribution shown on the About page.
inline QUrl upstreamUrl() { return QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo")); }
inline QString attribution() {
    return QStringLiteral("Veyra (Likely7) tabanlıdır · <a href=\"%1\">%1</a> · GPL-3.0").arg(upstreamUrl().toString());
}

// Exposed to QML as the `crispBrand` context property.
inline QVariantMap qmlProperties() {
    return QVariantMap{
        {QStringLiteral("appName"), appName()},
        {QStringLiteral("homeTitle"), homeTitle()},
        {QStringLiteral("attribution"), attribution()},
        {QStringLiteral("repoUrl"), repoUrl()},
        {QStringLiteral("upstreamUrl"), upstreamUrl()},
    };
}

// Versions are compared as four numbers: upstream major.minor.patch plus our
// patch level N from the "-crispplay.N" suffix (0 when there is none).
//   remote tag:  v2.1.6 or v2.1.6-crispplay.3 (anything else is rejected,
//                so -beta / -sources tags never count as releases)
//   local label: 2.1.6, 2.1.6-crispplay.3, or 2.1.6-<any test label> (N = 0)
inline QVersionNumber releaseVersion(const QString& text, bool local) {
    static const QRegularExpression remoteRx(
        QStringLiteral("^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(?:-crispplay\\.([0-9]+))?$"));
    static const QRegularExpression localRx(
        QStringLiteral("^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(?:\\.0)?(?:-crispplay\\.([0-9]+)|-[A-Za-z0-9.-]+)?$"));
    const auto m = (local ? localRx : remoteRx).match(text);
    if (!m.hasMatch()) return {};
    QList<int> parts;
    for (int i = 1; i <= 4; ++i) {
        const QString piece = m.captured(i);
        if (piece.isEmpty()) { parts.append(0); continue; }
        bool ok = false;
        const int value = piece.toInt(&ok);
        if (!ok) return {};   // overflow
        parts.append(value);
    }
    return QVersionNumber(parts);
}

// "2.1.6.3" -> "2.1.6-crispplay.3", "2.1.6.0" -> "2.1.6"
inline QString displayVersion(const QVersionNumber& v) {
    if (v.segmentCount() != 4) return v.toString();
    const QString base = QStringLiteral("%1.%2.%3").arg(v.segmentAt(0)).arg(v.segmentAt(1)).arg(v.segmentAt(2));
    return v.segmentAt(3) == 0 ? base : base + QStringLiteral("-crispplay.%1").arg(v.segmentAt(3));
}

inline bool isReleaseUrl(const QUrl& url, const QString& tag) {
    return url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com")
        && url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasQuery() && !url.hasFragment()
        && url.path() == releaseTagPath(tag);
}

} // namespace crispplay::branding
