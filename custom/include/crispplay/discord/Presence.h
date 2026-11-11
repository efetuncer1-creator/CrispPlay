// Discord Rich Presence: pure payload and wire-format code (no I/O, no Qt object state).
//
// Privacy by construction: buildActivity() takes a PresenceInput, which holds
// only enums, booleans and a start time. There is no string field a file name,
// path, title, window title, host or account name could travel in, and every
// text in the output is produced from a fixed table in Presence.cpp.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace crispplay::discord {

enum class PrivacyLevel { Off = 0, Basic = 1, Detailed = 2 };

enum class SourceType { None, File, Image, Capture, Screen, Ps5, Xbox, Pc, Network };

struct PresenceInput {
    PrivacyLevel level = PrivacyLevel::Off;
    SourceType source = SourceType::None;
    bool superResolution = false;
    bool noiseReduction = false;
    bool frameGeneration = false;
    qint64 startedAtUnix = 0;   // seconds since epoch; 0 = no elapsed timer

    bool operator==(const PresenceInput&) const = default;
};

PrivacyLevel levelFromString(const QString& text);   // unknown -> Off
QString levelToString(PrivacyLevel level);           // "off" | "basic" | "detailed"

// Maps QmlPlayerBridge::sourceKind() ("file", "capture", "ps5", ...) to a type.
// Unknown kinds map to None, so nothing is shown for a source we cannot name.
SourceType sourceTypeFromBridgeKind(const QString& kind);
QString sourceTypeLabel(SourceType type);            // Turkish, e.g. "Yakalama kartı"

// nullopt means "clear the activity" (level Off, or nothing is playing).
std::optional<QJsonObject> buildActivity(const PresenceInput& input);

// --- IPC wire format --------------------------------------------------------

enum Opcode : quint32 { OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4 };

QByteArray buildHandshake(const QString& clientId);
QByteArray buildSetActivity(quint32 pid, const std::optional<QJsonObject>& activity, const QString& nonce);
// Discord application IDs are decimal snowflakes.
bool isValidClientId(const QString& clientId);

// Frame = little-endian uint32 opcode + little-endian uint32 length + UTF-8 payload.
QByteArray encodeFrame(quint32 opcode, const QByteArray& payload);

struct Frame {
    quint32 opcode = 0;
    QByteArray payload;
};

// Reassembles frames from a byte stream. A header announcing more than
// kMaxPayload marks the stream as broken (failed() = true) instead of
// allocating whatever a misbehaving peer asks for.
class FrameParser {
public:
    static constexpr quint32 kMaxPayload = 1u << 20;
    void append(const QByteArray& bytes);
    bool next(Frame& out);
    bool failed() const { return failed_; }
private:
    QByteArray buffer_;
    bool failed_ = false;
};

} // namespace crispplay::discord
