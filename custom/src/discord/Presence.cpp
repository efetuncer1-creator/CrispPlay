#include "crispplay/discord/Presence.h"

#include <QJsonDocument>
#include <QStringList>

namespace crispplay::discord {

namespace {
constexpr auto kAssetKey = "crispplay";   // asset key uploaded in the Developer Portal (custom/DISCORD.md)
constexpr int kActivityTypeWatching = 3;

void putLe32(QByteArray& out, quint32 v) {
    for (int i = 0; i < 4; ++i) out.append(char((v >> (8 * i)) & 0xFF));
}
quint32 getLe32(const char* p) {
    quint32 v = 0;
    for (int i = 0; i < 4; ++i) v |= quint32(quint8(p[i])) << (8 * i);
    return v;
}
} // namespace

PrivacyLevel levelFromString(const QString& text) {
    if (text == QLatin1String("basic")) return PrivacyLevel::Basic;
    if (text == QLatin1String("detailed")) return PrivacyLevel::Detailed;
    return PrivacyLevel::Off;
}

QString levelToString(PrivacyLevel level) {
    switch (level) {
    case PrivacyLevel::Basic: return QStringLiteral("basic");
    case PrivacyLevel::Detailed: return QStringLiteral("detailed");
    default: return QStringLiteral("off");
    }
}

SourceType sourceTypeFromBridgeKind(const QString& kind) {
    if (kind == QLatin1String("file")) return SourceType::File;
    if (kind == QLatin1String("image")) return SourceType::Image;
    if (kind == QLatin1String("capture")) return SourceType::Capture;
    if (kind == QLatin1String("screen")) return SourceType::Screen;
    if (kind == QLatin1String("ps5")) return SourceType::Ps5;
    if (kind == QLatin1String("xbox")) return SourceType::Xbox;
    if (kind == QLatin1String("moonlight")) return SourceType::Pc;
    if (kind == QLatin1String("url")) return SourceType::Network;
    return SourceType::None;
}

QString sourceTypeLabel(SourceType type) {
    switch (type) {
    case SourceType::File: return QStringLiteral("Video dosyası");
    case SourceType::Image: return QStringLiteral("Görsel");
    case SourceType::Capture: return QStringLiteral("Yakalama kartı");
    case SourceType::Screen: return QStringLiteral("Ekran yakalama");
    case SourceType::Ps5: return QStringLiteral("PS5 yayını");
    case SourceType::Xbox: return QStringLiteral("Xbox yayını");
    case SourceType::Pc: return QStringLiteral("PC yayını");
    case SourceType::Network: return QStringLiteral("Ağ videosu");
    case SourceType::None: break;
    }
    return {};
}

std::optional<QJsonObject> buildActivity(const PresenceInput& in) {
    if (in.level == PrivacyLevel::Off || in.source == SourceType::None) return std::nullopt;

    QJsonObject activity;
    activity.insert(QStringLiteral("type"), kActivityTypeWatching);
    activity.insert(QStringLiteral("details"), QStringLiteral("CrispPlay ile izliyor"));

    if (in.level == PrivacyLevel::Detailed) {
        QStringList enhancements;
        if (in.superResolution) enhancements << QStringLiteral("SR");
        if (in.noiseReduction) enhancements << QStringLiteral("NR");
        if (in.frameGeneration) enhancements << QStringLiteral("FG");
        QString state = sourceTypeLabel(in.source);
        if (!enhancements.isEmpty()) state += QStringLiteral(" · ") + enhancements.join(QStringLiteral(" + "));
        activity.insert(QStringLiteral("state"), state);
    }

    if (in.startedAtUnix > 0)
        activity.insert(QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), in.startedAtUnix}});
    activity.insert(QStringLiteral("assets"), QJsonObject{
        {QStringLiteral("large_image"), QLatin1String(kAssetKey)},
        {QStringLiteral("large_text"), QStringLiteral("CrispPlay")},
    });
    return activity;
}

bool isValidClientId(const QString& id) {
    if (id.size() < 5 || id.size() > 24) return false;
    for (const QChar c : id)
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    return true;
}

QByteArray buildHandshake(const QString& clientId) {
    return QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), clientId}})
        .toJson(QJsonDocument::Compact);
}

QByteArray buildSetActivity(quint32 pid, const std::optional<QJsonObject>& activity, const QString& nonce) {
    QJsonObject args{{QStringLiteral("pid"), qint64(pid)}};
    args.insert(QStringLiteral("activity"), activity ? QJsonValue(*activity) : QJsonValue(QJsonValue::Null));
    return QJsonDocument(QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), args},
        {QStringLiteral("nonce"), nonce},
    }).toJson(QJsonDocument::Compact);
}

QByteArray encodeFrame(quint32 opcode, const QByteArray& payload) {
    QByteArray out;
    out.reserve(8 + payload.size());
    putLe32(out, opcode);
    putLe32(out, quint32(payload.size()));
    out.append(payload);
    return out;
}

void FrameParser::append(const QByteArray& bytes) {
    if (!failed_) buffer_.append(bytes);
}

bool FrameParser::next(Frame& out) {
    if (failed_ || buffer_.size() < 8) return false;
    const quint32 len = getLe32(buffer_.constData() + 4);
    if (len > kMaxPayload) { failed_ = true; buffer_.clear(); return false; }
    if (quint32(buffer_.size()) < 8 + len) return false;
    out.opcode = getLe32(buffer_.constData());
    out.payload = buffer_.mid(8, int(len));
    buffer_.remove(0, int(8 + len));
    return true;
}

} // namespace crispplay::discord
