#include "crispplay/subs/ocr/PpOcr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

#if CRISPPLAY_SUBS_HAVE_ORT
#include <windows.h>
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <mutex>
#endif

namespace crispplay::subs::ocr {

PpOcrReading ctcGreedyDecode(const float* probs, int steps, int classes, const std::vector<std::string>& dict) {
    PpOcrReading out;
    if (!probs || steps <= 0 || classes <= 0) return out;
    double confSum = 0;
    int kept = 0;
    int prev = -1;
    for (int t = 0; t < steps; ++t) {
        const float* row = probs + size_t(t) * size_t(classes);
        const int best = int(std::max_element(row, row + classes) - row);
        if (best != 0 && best != prev) {
            const size_t idx = size_t(best);
            if (idx <= dict.size()) out.text += dict[idx - 1];
            else if (idx == dict.size() + 1) out.text += ' ';
            confSum += row[best];
            ++kept;
        }
        prev = best;
    }
    out.confidence = kept ? float(confSum / kept) : 0.f;
    return out;
}

namespace {

// Python round(): half to even.
int roundTo32(double v) { return int(std::nearbyint(v / 32.0) * 32.0); }

}  // namespace

std::vector<float> ppocrInput(const BandImage& line, int height, int* width) {
    if (width) *width = 0;
    if (line.empty() || height <= 0) return {};
    // RapidOCR resize_image_within_bounds(min_side_len=30, max_side_len=2000).
    BandImage img;
    const BandImage* src = &line;
    {
        int h = line.height, w = line.width;
        if (std::max(h, w) > 2000) {
            const double r = 2000.0 / std::max(h, w);
            const int rh = roundTo32(int(h * r)), rw = roundTo32(int(w * r));
            if (rh > 0 && rw > 0) {
                img = resizeBilinear(*src, rw, rh);
                src = &img;
            }
        }
        h = src->height;
        w = src->width;
        if (std::min(h, w) < 30) {
            const double r = 30.0 / std::min(h, w);
            const int rh = roundTo32(int(h * r)), rw = roundTo32(int(w * r));
            if (rh > 0 && rw > 0) {
                BandImage up = resizeBilinear(*src, rw, rh);
                img = std::move(up);
                src = &img;
            }
        }
    }
    // PaddleOCR resize_norm_img with rec_img_shape [3, 48, 320], one image per batch.
    const double ratio = double(src->width) / src->height;
    const double maxRatio = std::max(320.0 / 48.0, ratio);
    const int padW = int(height * maxRatio);
    const int resizedW = std::min(padW, int(std::ceil(height * ratio)));
    const BandImage resized = resizeBilinear(*src, std::max(1, resizedW), height);
    std::vector<float> chw(size_t(3) * height * padW, 0.f);
    for (int y = 0; y < height; ++y) {
        const uint8_t* p = resized.row(y);
        for (int x = 0; x < resized.width; ++x, p += 4)
            for (int c = 0; c < 3; ++c)  // BGR order, as cv2 hands it to the network
                chw[(size_t(c) * height + y) * padW + x] = p[c] / 127.5f - 1.f;
    }
    if (width) *width = padW;
    return chw;
}

#if CRISPPLAY_SUBS_HAVE_ORT

namespace {

std::filesystem::path exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
}

// Loads onnxruntime.dll once per process and initialises the C++ API table.
bool loadRuntime(const std::filesystem::path& dir, std::string* error) {
    static std::mutex mutex;
    static bool loaded = false;
    static std::string loadError;
    std::lock_guard lock(mutex);
    if (loaded) return true;
    const auto dll = (dir.empty() ? exeDir() : dir) / L"onnxruntime.dll";
    HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        if (error) *error = "ONNX Runtime not found: " + dll.string();
        return false;
    }
    using GetApiBase = const OrtApiBase*(ORT_API_CALL*)();
    const auto getBase = reinterpret_cast<GetApiBase>(GetProcAddress(module, "OrtGetApiBase"));
    const OrtApi* api = getBase ? getBase()->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        if (error)
            *error = "ONNX Runtime at " + dll.string() + " is too old (needs API " + std::to_string(ORT_API_VERSION) +
                     ", version " + (getBase ? std::string(getBase()->GetVersionString()) : std::string("?")) + ")";
        FreeLibrary(module);
        return false;
    }
    Ort::InitApi(api);
    loaded = true;
    return true;
}

Ort::Env& env() {
    static Ort::Env e(ORT_LOGGING_LEVEL_WARNING, "crispplay-subs-ocr");
    return e;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

}  // namespace

struct PpOcrRecognizer::Impl {
    std::unique_ptr<Ort::Session> session;
    std::string inputName, outputName;
    std::vector<std::string> dict;
    int height = 48;
};

PpOcrRecognizer::PpOcrRecognizer() : impl_(std::make_unique<Impl>()) {}
PpOcrRecognizer::~PpOcrRecognizer() = default;
bool PpOcrRecognizer::runtimeCompiledIn() { return true; }

std::unique_ptr<PpOcrRecognizer> PpOcrRecognizer::create(const PpOcrOptions& options, std::string* error) {
    std::error_code ec;
    if (options.modelPath.empty() || !std::filesystem::is_regular_file(options.modelPath, ec)) {
        if (error) *error = "PP-OCR model not found: " + options.modelPath.string();
        return nullptr;
    }
    if (!loadRuntime(options.runtimeDir, error)) return nullptr;
    std::unique_ptr<PpOcrRecognizer> rec(new PpOcrRecognizer());
    auto& d = *rec->impl_;
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(std::max(1, options.threads));
        so.SetInterOpNumThreads(1);
        so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.DisableCpuMemArena();
        d.session = std::make_unique<Ort::Session>(env(), options.modelPath.c_str(), so);
        Ort::AllocatorWithDefaultOptions alloc;
        d.inputName = d.session->GetInputNameAllocated(0, alloc).get();
        d.outputName = d.session->GetOutputNameAllocated(0, alloc).get();
        const auto inShape = d.session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inShape.size() == 4 && inShape[2] > 0) d.height = int(inShape[2]);
        if (!options.dictPath.empty()) {
            std::ifstream f(options.dictPath, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            d.dict = splitLines(ss.str());
        } else {
            const auto meta = d.session->GetModelMetadata();
            const auto chars = meta.LookupCustomMetadataMapAllocated("character", alloc);
            if (chars) d.dict = splitLines(chars.get());
        }
        if (d.dict.empty()) {
            if (error) *error = "PP-OCR model has no character list and no dictionary file was given";
            return nullptr;
        }
        const auto outShape = d.session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (!outShape.empty() && outShape.back() > 0 && size_t(outShape.back()) != d.dict.size() + 2) {
            if (error)
                *error = "PP-OCR dictionary (" + std::to_string(d.dict.size()) + " + 2) does not match the model output (" +
                         std::to_string(outShape.back()) + ")";
            return nullptr;
        }
    } catch (const Ort::Exception& e) {
        if (error) *error = std::string("PP-OCR model failed to load: ") + e.what();
        return nullptr;
    }
    return rec;
}

int PpOcrRecognizer::inputHeight() const { return impl_->height; }
size_t PpOcrRecognizer::dictionarySize() const { return impl_->dict.size(); }

PpOcrReading PpOcrRecognizer::recognize(const BandImage& line) {
    int w = 0;
    std::vector<float> input = ppocrInput(line, impl_->height, &w);
    if (input.empty()) return {};
    try {
        const std::array<int64_t, 4> shape{1, 3, impl_->height, w};
        const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
        const char* inNames[] = {impl_->inputName.c_str()};
        const char* outNames[] = {impl_->outputName.c_str()};
        auto out = impl_->session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
        const auto s = out[0].GetTensorTypeAndShapeInfo().GetShape();
        if (s.size() != 3) return {};
        return ctcGreedyDecode(out[0].GetTensorData<float>(), int(s[1]), int(s[2]), impl_->dict);
    } catch (const Ort::Exception&) {
        return {};
    }
}

#else  // !CRISPPLAY_SUBS_HAVE_ORT

struct PpOcrRecognizer::Impl {};
PpOcrRecognizer::PpOcrRecognizer() : impl_(std::make_unique<Impl>()) {}
PpOcrRecognizer::~PpOcrRecognizer() = default;
bool PpOcrRecognizer::runtimeCompiledIn() { return false; }
std::unique_ptr<PpOcrRecognizer> PpOcrRecognizer::create(const PpOcrOptions&, std::string* error) {
    if (error) *error = "built without ONNX Runtime: PP-OCR reader disabled, Windows OCR only";
    return nullptr;
}
int PpOcrRecognizer::inputHeight() const { return 48; }
size_t PpOcrRecognizer::dictionarySize() const { return 0; }
PpOcrReading PpOcrRecognizer::recognize(const BandImage&) { return {}; }

#endif

}  // namespace crispplay::subs::ocr
