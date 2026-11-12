// Part of CrispPlay, GPL-3.0 (see LICENSE). Written for CrispPlay; the DeepL request shape
// (source/target language, formality "prefer_less", ":fx" free host) follows the public
// DeepL API documentation, as CouchSubs' DeepLTranslator.cs does, but no code was taken.
//
// Privacy: nothing here logs. A request leaves the machine only from translate() of a
// translator that makeOnlineTranslator() created, which needs provider != None and a stored key.
#include "OnlineInternal.h"
#include "WinHttpPost.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <windows.h>
#include <dpapi.h>

#include <algorithm>

namespace crispplay::subs {
namespace online {

const char kSubtitleSystemPrompt[] =
    "You translate English video game subtitles into Turkish. Reply with the Turkish translation only: "
    "no quotes, notes, alternatives or explanations. Write short, natural Turkish as a subtitle would. "
    "Address the listener informally with \"sen\" and keep it consistent; use \"siz\" only for a clearly "
    "formal or plural address, never mix the two within a line. Keep character names, place names, "
    "item names and other proper nouns as they are. If the line starts with a speaker label such as "
    "\"NAME:\", keep that label unchanged. Keep line breaks.";

namespace {

constexpr char kEntropy[] = "CrispPlay.subs.online.key.v1";
constexpr qint64 kMaxConfigBytes = 64 * 1024;
constexpr qint64 kMaxKeyFileBytes = 16 * 1024;

QString dataDir(const std::string& utf8) { return QString::fromUtf8(utf8.data(), qsizetype(utf8.size())); }

const char* providerName(OnlineProvider p) {
    switch (p) {
    case OnlineProvider::DeepL: return "deepl";
    case OnlineProvider::OpenAICompatible: return "openai-compatible";
    default: return "none";
    }
}

OnlineProvider providerFromName(const QString& s) {
    if (s == QLatin1String("deepl")) return OnlineProvider::DeepL;
    if (s == QLatin1String("openai-compatible")) return OnlineProvider::OpenAICompatible;
    return OnlineProvider::None;
}

std::string trimmed(const std::string& s) { return QString::fromUtf8(s).trimmed().toStdString(); }

void wipe(std::string& s) {
    if (!s.empty()) SecureZeroMemory(s.data(), s.size());
    s.clear();
}
void wipe(std::wstring& s) {
    if (!s.empty()) SecureZeroMemory(s.data(), s.size() * sizeof(wchar_t));
    s.clear();
}
void wipe(QByteArray& b) {
    if (!b.isEmpty()) SecureZeroMemory(b.data(), size_t(b.size()));
    b.clear();
}

bool blankText(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
}

// "https://host/base/" + "/v2/translate" without doubling slashes.
QUrl withPath(const QString& base, const QString& suffix) {
    QString b = base.trimmed();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return QUrl(b + suffix, QUrl::StrictMode);
}

} // namespace

QString configPath(const std::string& d) { return dataDir(d) + QStringLiteral("/crispplay-subs-online.v1.json"); }

QString keyPath(const std::string& d, OnlineProvider provider) {
    return dataDir(d) + QStringLiteral("/crispplay-subs-online-%1.key").arg(QLatin1String(providerName(provider)));
}

std::optional<std::string> loadApiKey(const std::string& d, OnlineProvider provider) {
    if (provider == OnlineProvider::None) return std::nullopt;
    QFile f(keyPath(d, provider));
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxKeyFileBytes) return std::nullopt;
    QByteArray blob = f.readAll();
    DATA_BLOB in{DWORD(blob.size()), reinterpret_cast<BYTE*>(blob.data())};
    DATA_BLOB entropy{DWORD(sizeof kEntropy - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string key(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (key.empty()) return std::nullopt;
    return key;
}

QUrl deeplUrl(const OnlineConfig& config, const std::string& key) {
    QUrl url;
    if (config.endpoint.empty()) {
        const bool free = key.size() >= 3 && key.compare(key.size() - 3, 3, ":fx") == 0;
        url = QUrl(free ? QStringLiteral("https://api-free.deepl.com/v2/translate")
                        : QStringLiteral("https://api.deepl.com/v2/translate"));
    } else {
        const QString base = QString::fromStdString(config.endpoint).trimmed();
        url = base.endsWith(QLatin1String("/translate")) ? QUrl(base, QUrl::StrictMode) : withPath(base, QStringLiteral("/v2/translate"));
    }
    return detail::onlineEndpointAllowed(url) ? url : QUrl();
}

QUrl openAiUrl(const OnlineConfig& config) {
    const QString base = QString::fromStdString(config.endpoint).trimmed();
    if (base.isEmpty()) return {};
    const QUrl url = base.endsWith(QLatin1String("/chat/completions")) ? QUrl(base, QUrl::StrictMode)
                                                                        : withPath(base, QStringLiteral("/chat/completions"));
    return detail::onlineEndpointAllowed(url) ? url : QUrl();
}

QByteArray deeplRequestBody(const std::string& english) {
    const QJsonObject o{
        {QStringLiteral("text"), QJsonArray{QString::fromUtf8(english)}},
        {QStringLiteral("source_lang"), QStringLiteral("EN")},
        {QStringLiteral("target_lang"), QStringLiteral("TR")},
        // "sen" rather than "siz" where DeepL supports it; ignored where it does not.
        {QStringLiteral("formality"), QStringLiteral("prefer_less")},
        {QStringLiteral("preserve_formatting"), true},
    };
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<std::string> parseDeeplResponse(const QByteArray& body) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonArray list = doc.object().value(QStringLiteral("translations")).toArray();
    if (list.size() != 1) return std::nullopt;
    const QJsonValue text = list.at(0).toObject().value(QStringLiteral("text"));
    if (!text.isString()) return std::nullopt;
    const QString t = text.toString().trimmed();
    if (t.isEmpty()) return std::nullopt;
    return t.toStdString();
}

int openAiMaxTokens(const std::string& english) {
    // Turkish needs roughly 1.5-2x the tokens of the English line; bounded either way.
    return std::clamp(int(english.size()) + 64, 96, 512);
}

QByteArray openAiRequestBody(const std::string& model, const std::string& english) {
    const QJsonObject o{
        {QStringLiteral("model"), QString::fromStdString(model)},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), QString::fromUtf8(kSubtitleSystemPrompt)}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), QString::fromUtf8(english)}}}},
        {QStringLiteral("temperature"), 0},
        {QStringLiteral("max_tokens"), openAiMaxTokens(english)},
        {QStringLiteral("stream"), false},
    };
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<std::string> parseOpenAiResponse(const QByteArray& body) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return std::nullopt;
    const QJsonObject choice = choices.at(0).toObject();
    // A cut-off answer is a wrong subtitle: let the local engine take this line.
    if (choice.value(QStringLiteral("finish_reason")).toString() == QLatin1String("length")) return std::nullopt;
    const QJsonValue content = choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content"));
    if (!content.isString()) return std::nullopt;
    const QString t = content.toString().trimmed();
    if (t.isEmpty()) return std::nullopt;
    return t.toStdString();
}

} // namespace online

namespace {

class HttpTranslator : public Translator {
public:
    HttpTranslator(QUrl url, std::string key, int timeoutMs) : url_(std::move(url)), key_(std::move(key)), timeoutMs_(timeoutMs) {}
    ~HttpTranslator() override { online::wipe(key_); }

    std::optional<std::string> translate(const std::string& english) override {
        if (online::blankText(english)) return std::string();
        if (english.size() > size_t(online::kMaxInputBytes)) return std::nullopt;
        QByteArray body = requestBody(english);
        std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n" + authHeader();
        const auto response = http_.post(url_, headers, body, timeoutMs_);
        online::wipe(headers);
        online::wipe(body);
        if (!response || response->status != 200) return std::nullopt;
        return parse(response->body);
    }
    void cancel() override { http_.cancel(); }

protected:
    virtual QByteArray requestBody(const std::string& english) const = 0;
    virtual std::wstring authHeader() const = 0;
    virtual std::optional<std::string> parse(const QByteArray& body) const = 0;

    std::wstring keyWide() const { return QString::fromStdString(key_).toStdWString(); }

private:
    QUrl url_;
    std::string key_;
    int timeoutMs_;
    detail::WinHttpPost http_;
};

class DeepLTranslator final : public HttpTranslator {
public:
    using HttpTranslator::HttpTranslator;

protected:
    QByteArray requestBody(const std::string& english) const override { return online::deeplRequestBody(english); }
    std::wstring authHeader() const override { return L"Authorization: DeepL-Auth-Key " + keyWide() + L"\r\n"; }
    std::optional<std::string> parse(const QByteArray& body) const override { return online::parseDeeplResponse(body); }
};

class OpenAiTranslator final : public HttpTranslator {
public:
    OpenAiTranslator(QUrl url, std::string key, int timeoutMs, std::string model)
        : HttpTranslator(std::move(url), std::move(key), timeoutMs), model_(std::move(model)) {}

protected:
    QByteArray requestBody(const std::string& english) const override { return online::openAiRequestBody(model_, english); }
    std::wstring authHeader() const override { return L"Authorization: Bearer " + keyWide() + L"\r\n"; }
    std::optional<std::string> parse(const QByteArray& body) const override { return online::parseOpenAiResponse(body); }

private:
    std::string model_;
};

} // namespace

OnlineConfig loadOnlineConfig(const std::string& dataDirectoryUtf8) {
    OnlineConfig c;
    QFile f(online::configPath(dataDirectoryUtf8));
    if (!f.open(QIODevice::ReadOnly) || f.size() > online::kMaxConfigBytes) return c;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("version")).toInt() != 1) return c;
    c.provider = online::providerFromName(o.value(QStringLiteral("provider")).toString());
    c.endpoint = o.value(QStringLiteral("endpoint")).toString().trimmed().left(512).toStdString();
    c.model = o.value(QStringLiteral("model")).toString().trimmed().left(128).toStdString();
    c.timeoutMs = std::clamp(o.value(QStringLiteral("timeoutMs")).toInt(2500), 500, 30000);
    return c;
}

bool saveOnlineConfig(const std::string& dataDirectoryUtf8, const OnlineConfig& config) {
    if (!QDir().mkpath(online::dataDir(dataDirectoryUtf8))) return false;
    const QJsonObject o{
        {QStringLiteral("version"), 1},
        {QStringLiteral("provider"), QLatin1String(online::providerName(config.provider))},
        {QStringLiteral("endpoint"), QString::fromStdString(online::trimmed(config.endpoint)).left(512)},
        {QStringLiteral("model"), QString::fromStdString(online::trimmed(config.model)).left(128)},
        {QStringLiteral("timeoutMs"), std::clamp(config.timeoutMs, 500, 30000)},
    };
    QSaveFile f(online::configPath(dataDirectoryUtf8));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

bool saveApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider, const std::string& keyIn) {
    if (provider == OnlineProvider::None) return false;
    std::string key = online::trimmed(keyIn);
    if (key.empty()) {
        clearApiKey(dataDirectoryUtf8, provider);
        return true;
    }
    if (!QDir().mkpath(online::dataDir(dataDirectoryUtf8))) {
        online::wipe(key);
        return false;
    }
    DATA_BLOB in{DWORD(key.size()), reinterpret_cast<BYTE*>(key.data())};
    DATA_BLOB entropy{DWORD(sizeof online::kEntropy - 1), reinterpret_cast<BYTE*>(const_cast<char*>(online::kEntropy))};
    DATA_BLOB out{};
    const BOOL ok = CryptProtectData(&in, L"CrispPlay online translation key", &entropy, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &out);
    online::wipe(key);
    if (!ok) return false;
    const QByteArray blob(reinterpret_cast<const char*>(out.pbData), qsizetype(out.cbData));
    LocalFree(out.pbData);
    QSaveFile f(online::keyPath(dataDirectoryUtf8, provider));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(blob);
    return f.commit();
}

bool hasApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider) {
    auto key = online::loadApiKey(dataDirectoryUtf8, provider);
    if (!key) return false;
    online::wipe(*key);
    return true;
}

void clearApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider) {
    if (provider == OnlineProvider::None) return;
    QFile::remove(online::keyPath(dataDirectoryUtf8, provider));
}

std::unique_ptr<Translator> makeOnlineTranslator(const std::string& dataDirectoryUtf8, const OnlineConfig& config) {
    if (config.provider == OnlineProvider::None) return nullptr;
    auto key = online::loadApiKey(dataDirectoryUtf8, config.provider);
    if (!key) return nullptr;
    const int timeout = std::clamp(config.timeoutMs, 500, 30000);
    std::unique_ptr<Translator> t;
    if (config.provider == OnlineProvider::DeepL) {
        const QUrl url = online::deeplUrl(config, *key);
        if (url.isValid()) t = std::make_unique<DeepLTranslator>(url, std::move(*key), timeout);
    } else if (config.provider == OnlineProvider::OpenAICompatible) {
        const QUrl url = online::openAiUrl(config);
        const std::string model = online::trimmed(config.model);
        if (url.isValid() && !model.empty()) t = std::make_unique<OpenAiTranslator>(url, std::move(*key), timeout, model);
    }
    if (key) online::wipe(*key);
    return t;
}

} // namespace crispplay::subs
