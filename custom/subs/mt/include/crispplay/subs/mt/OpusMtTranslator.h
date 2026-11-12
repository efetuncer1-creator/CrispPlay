#pragma once

// English -> Turkish subtitle translation with OPUS-MT (opus-mt-tc-big-en-tr)
// on CTranslate2 (CPU, int8) + SentencePiece. Decision and numbers:
// custom/docs/SUBS_SPIKE.md, custom/subs/mt/README.md.

#include <crispplay/subs/mt/TranslatorInterface.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace crispplay::subs::mt {

struct OpusMtOptions {
    std::size_t intraThreads = 2; // OpenMP threads inside one translation
    std::size_t beamSize = 4;
    std::size_t maxDecodingLength = 128; // target tokens per sentence
    std::size_t maxSourceTokens = 128;   // longer sentences are cut (pieces, before </s>)
    std::size_t maxInputBytes = 1000;    // the whole line is cut to this many UTF-8 bytes first
    bool splitSentences = true;          // false: translate the line in one piece
};

class OpusMtTranslator final : public Translator {
public:
    // modelDir holds model.bin, config.json, shared_vocabulary.json, source.spm, target.spm.
    // Nothing is read here; the model loads on the first translate() or load() call.
    explicit OpusMtTranslator(std::filesystem::path modelDir, OpusMtOptions options = {});
    ~OpusMtTranslator() override;

    OpusMtTranslator(const OpusMtTranslator&) = delete;
    OpusMtTranslator& operator=(const OpusMtTranslator&) = delete;

    // Loads the model (blocking, call on the worker). Idempotent; a failed load is
    // remembered and not retried. Returns false when the model is unusable.
    bool load();
    bool isLoaded() const;

    // Translation of one subtitle line. nullopt when the model is missing/broken,
    // the call was cancelled, or CTranslate2 threw. Empty/whitespace input gives "".
    // The output may contain U+2047 for pieces the model does not know; the caller
    // decides what to show (see lastHadUnknown / containsUnknownMarker).
    std::optional<std::string> translate(const std::string& english) override;

    // Makes a translate() that is running now return nullopt (the sentences of one
    // line decode as one batch, so it takes effect when that batch ends, <~0.3 s).
    // Calls that start later are not affected. Thread-safe.
    void cancel() override;

    // For logs. Milliseconds; -1 until known.
    double loadTimeMs() const { return loadMs_.load(); }
    double lastLatencyMs() const { return lastMs_.load(); }
    // True when the last successful translate() produced an <unk> target piece.
    bool lastHadUnknown() const { return lastUnk_.load(); }
    // Why load() failed (empty when it did not).
    std::string lastError() const;

    const OpusMtOptions& options() const { return options_; }

private:
    struct Impl;

    std::optional<std::vector<std::string>> translateSentences(const std::vector<std::string>& sentences, bool& unk);

    std::filesystem::path modelDir_;
    OpusMtOptions options_;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_; // serialises load() and translate()
    enum class State { Unloaded, Loaded, Failed };
    std::atomic<State> state_{State::Unloaded};
    std::string error_;
    std::atomic<unsigned> cancelGeneration_{0};
    std::atomic<double> loadMs_{-1.0};
    std::atomic<double> lastMs_{-1.0};
    std::atomic<bool> lastUnk_{false};
};

} // namespace crispplay::subs::mt
