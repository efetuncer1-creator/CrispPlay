#include <crispplay/subs/mt/OpusMtTranslator.h>
#include <crispplay/subs/mt/TextSplit.h>

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace crispplay::subs::mt {
namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// SentencePiece's own file opening is not Unicode-safe on Windows; read the bytes
// through std::filesystem and hand over the serialized proto.
bool readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return static_cast<bool>(in) || in.eof();
}

std::string pathUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace

struct OpusMtTranslator::Impl {
    std::unique_ptr<ctranslate2::Translator> translator;
    sentencepiece::SentencePieceProcessor source;
    sentencepiece::SentencePieceProcessor target;
    ctranslate2::TranslationOptions translation;
};

OpusMtTranslator::OpusMtTranslator(std::filesystem::path modelDir, OpusMtOptions options)
    : modelDir_(std::move(modelDir)), options_(options)
{
}

OpusMtTranslator::~OpusMtTranslator() = default;

bool OpusMtTranslator::isLoaded() const
{
    return state_.load() == State::Loaded;
}

std::string OpusMtTranslator::lastError() const
{
    std::lock_guard lock(mutex_);
    return error_;
}

bool OpusMtTranslator::load()
{
    std::lock_guard lock(mutex_);
    if (state_ != State::Unloaded) return state_ == State::Loaded;

    const auto t0 = Clock::now();
    auto impl = std::make_unique<Impl>();
    auto fail = [&](std::string why) {
        error_ = std::move(why);
        state_ = State::Failed;
        return false;
    };

    std::error_code ec;
    for (const char* name : {"model.bin", "config.json", "source.spm", "target.spm"}) {
        if (!std::filesystem::is_regular_file(modelDir_ / name, ec)) {
            return fail("missing " + pathUtf8(modelDir_ / name));
        }
    }

    std::string bytes;
    for (auto [proc, name] : {std::pair{&impl->source, "source.spm"}, std::pair{&impl->target, "target.spm"}}) {
        if (!readFile(modelDir_ / name, bytes)) return fail(std::string("cannot read ") + name);
        const auto status = proc->LoadFromSerializedProto(bytes);
        if (!status.ok()) return fail(std::string(name) + ": " + status.ToString());
    }

    try {
        ctranslate2::ReplicaPoolConfig pool;
        pool.num_threads_per_replica = options_.intraThreads;
        impl->translator = std::make_unique<ctranslate2::Translator>(
            pathUtf8(modelDir_), ctranslate2::Device::CPU, ctranslate2::ComputeType::INT8,
            std::vector<int>{0}, false, pool);
    } catch (const std::exception& e) {
        return fail(std::string("CTranslate2: ") + e.what());
    }

    auto& t = impl->translation;
    t.beam_size = options_.beamSize;
    t.max_decoding_length = options_.maxDecodingLength;
    t.repetition_penalty = 1.0f;

    impl_ = std::move(impl);
    loadMs_ = msSince(t0);
    state_ = State::Loaded;
    error_.clear();
    return true;
}

std::optional<std::vector<std::string>> OpusMtTranslator::translateSentences(const std::vector<std::string>& sentences,
                                                                            bool& unk)
{
    // One batch per subtitle, as in the spike: the sentences decode together.
    std::vector<std::vector<std::string>> batch;
    batch.reserve(sentences.size());
    for (const auto& s : sentences) {
        std::vector<std::string> pieces;
        if (!impl_->source.Encode(s, &pieces).ok()) return std::nullopt;
        if (pieces.size() > options_.maxSourceTokens) pieces.resize(options_.maxSourceTokens);
        pieces.emplace_back("</s>");
        batch.push_back(std::move(pieces));
    }

    const auto results = impl_->translator->translate_batch(batch, impl_->translation);
    if (results.size() != batch.size()) return std::nullopt;
    std::vector<std::string> out;
    out.reserve(results.size());
    for (const auto& r : results) {
        if (r.hypotheses.empty()) return std::nullopt;
        const auto& tokens = r.hypotheses.front();
        for (const auto& tok : tokens) {
            if (tok == "<unk>") unk = true;
        }
        std::string text;
        if (!impl_->target.Decode(tokens, &text).ok()) return std::nullopt;
        out.push_back(std::move(text));
    }
    return out;
}

std::optional<std::string> OpusMtTranslator::translate(const std::string& english)
{
    const unsigned generation = cancelGeneration_.load();
    if (!load()) return std::nullopt;

    std::lock_guard lock(mutex_);
    const auto t0 = Clock::now();
    const std::string line = truncateUtf8(trim(english), options_.maxInputBytes);
    if (line.empty()) {
        lastMs_ = 0.0;
        lastUnk_ = false;
        return std::string();
    }

    std::vector<std::string> sentences;
    if (options_.splitSentences) {
        sentences = splitSentences(line);
    } else {
        sentences.push_back(line);
    }

    bool unk = false;
    std::optional<std::vector<std::string>> out;
    if (cancelGeneration_.load() != generation) return std::nullopt;
    try {
        out = translateSentences(sentences, unk);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!out) return std::nullopt;
    if (cancelGeneration_.load() != generation) return std::nullopt;

    lastMs_ = msSince(t0);
    lastUnk_ = unk;
    return joinSentences(*out);
}

void OpusMtTranslator::cancel()
{
    cancelGeneration_.fetch_add(1);
}

} // namespace crispplay::subs::mt
