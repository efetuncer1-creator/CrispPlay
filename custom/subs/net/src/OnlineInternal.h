// Internals of the online translators, visible to the tests. Part of CrispPlay, GPL-3.0.
#pragma once

#include <crispplay/subs/OnlineTranslator.h>

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>
#include <string>

namespace crispplay::subs::online {

extern const char kSubtitleSystemPrompt[];
constexpr int kMaxInputBytes = 4000;  // a subtitle is never this long; longer input is not sent

QString configPath(const std::string& dataDirectoryUtf8);
QString keyPath(const std::string& dataDirectoryUtf8, OnlineProvider provider);
// DPAPI decrypted key, nullopt when missing/unreadable.
std::optional<std::string> loadApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider);

// Request targets; an invalid QUrl when the configuration cannot be used.
QUrl deeplUrl(const OnlineConfig& config, const std::string& key);  // ":fx" key -> api-free.deepl.com
QUrl openAiUrl(const OnlineConfig& config);                          // <base>/chat/completions

QByteArray deeplRequestBody(const std::string& english);
std::optional<std::string> parseDeeplResponse(const QByteArray& body);
QByteArray openAiRequestBody(const std::string& model, const std::string& english);
std::optional<std::string> parseOpenAiResponse(const QByteArray& body);
int openAiMaxTokens(const std::string& english);

} // namespace crispplay::subs::online
