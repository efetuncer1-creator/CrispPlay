// Phase 5 contract: optional online translation engine (off by default).
// Implemented by crispplay_subs_net. Nothing is sent anywhere unless the user
// enabled it in the settings and stored a key. The key is kept with Windows
// DPAPI (CryptProtectData, current user) in <dataDirectory>, never in plain text.
#pragma once

#include <crispplay/subs/Types.h>

#include <memory>
#include <string>

namespace crispplay::subs {

enum class OnlineProvider { None = 0, DeepL = 1, OpenAICompatible = 2 };

struct OnlineConfig {
    OnlineProvider provider = OnlineProvider::None;
    std::string endpoint;   // DeepL: api-free/api host chosen from the key; OpenAI-compatible: base URL
    std::string model;      // OpenAI-compatible only
    int timeoutMs = 2500;   // a late subtitle is useless
};

// Settings file <dataDirectory>/crispplay-subs-online.v1.json (+ DPAPI key blob).
OnlineConfig loadOnlineConfig(const std::string& dataDirectoryUtf8);
bool saveOnlineConfig(const std::string& dataDirectoryUtf8, const OnlineConfig& config);
bool saveApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider, const std::string& key);
bool hasApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider);
void clearApiKey(const std::string& dataDirectoryUtf8, OnlineProvider provider);

// nullptr when provider is None or no key is stored. Blocking translate(),
// called on the pipeline worker thread; returns nullopt on any error/timeout
// (the caller then falls back to the local engine).
std::unique_ptr<Translator> makeOnlineTranslator(const std::string& dataDirectoryUtf8, const OnlineConfig& config);

} // namespace crispplay::subs
