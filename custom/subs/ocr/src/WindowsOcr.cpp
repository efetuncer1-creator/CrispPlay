#include "crispplay/subs/ocr/WindowsOcr.h"

#include <windows.h>
#include <objbase.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace crispplay::subs::ocr {

namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmo = winrt::Windows::Media::Ocr;
using winrt::Windows::Globalization::Language;

namespace {

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool isEnglish(const std::string& tag) { return tag.size() >= 2 && (tag[0] == 'e' || tag[0] == 'E') && (tag[1] == 'n' || tag[1] == 'N') && (tag.size() == 2 || tag[2] == '-'); }

// Creates the engine and fills `status`; returns null when nothing can read.
wmo::OcrEngine createEngine(const std::string& languageTag, WindowsOcrStatus& status) {
    status = {};
    wmo::OcrEngine engine{nullptr};
    try {
        for (const auto& lang : wmo::OcrEngine::AvailableRecognizerLanguages())
            status.installedLanguages.push_back(winrt::to_string(lang.LanguageTag()));
        if (!languageTag.empty()) {
            const Language wanted{winrt::to_hstring(languageTag)};
            if (wmo::OcrEngine::IsLanguageSupported(wanted)) engine = wmo::OcrEngine::TryCreateFromLanguage(wanted);
        }
        if (!engine) engine = wmo::OcrEngine::TryCreateFromUserProfileLanguages();
    } catch (const winrt::hresult_error& e) {
        status.message = "Windows OCR unavailable: " + winrt::to_string(e.message());
        return nullptr;
    }
    if (!engine) {
        status.message = "Windows OCR has no installed recognizer language. Install the English (en-US) OCR "
                         "language pack: Settings > Time & language > Language & region > English (United States) > "
                         "Language options > Optical character recognition.";
        return nullptr;
    }
    status.available = true;
    status.languageTag = winrt::to_string(engine.RecognizerLanguage().LanguageTag());
    status.english = isEnglish(status.languageTag);
    if (!status.english) {
        status.message = "English OCR is not installed; Windows OCR falls back to \"" + status.languageTag +
                         "\" and will misread English subtitles. Install the English (en-US) OCR language pack "
                         "(Settings > Time & language > Language & region > English > Language options).";
    } else if (!languageTag.empty() && status.languageTag != languageTag) {
        status.message = "Requested \"" + languageTag + "\" is not installed; using \"" + status.languageTag + "\".";
    }
    return engine;
}

}  // namespace

struct WindowsOcrReader::Impl {
    WindowsOcrStatus status;
    wmo::OcrEngine engine{nullptr};
    wgi::SoftwareBitmap bitmap{nullptr};  // reused while the size stays the same
    int bitmapW = 0, bitmapH = 0;
};

bool WindowsOcrReader::prepareThread() {
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    const HRESULT hr = CoGetApartmentType(&type, &qualifier);
    if (hr == CO_E_NOTINITIALIZED) {
        // Stays initialised for the life of the thread, like any worker that uses WinRT.
        return SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    }
    if (FAILED(hr)) return false;
    return type != APTTYPE_STA && type != APTTYPE_MAINSTA;
}

WindowsOcrReader::WindowsOcrReader(const std::string& languageTag) : impl_(std::make_unique<Impl>()) {
    if (!prepareThread()) {
        impl_->status.message = "Windows OCR must run on a worker (MTA) thread, not the UI thread.";
        return;
    }
    impl_->engine = createEngine(languageTag, impl_->status);
}

WindowsOcrReader::~WindowsOcrReader() = default;

const WindowsOcrStatus& WindowsOcrReader::status() const { return impl_->status; }

WindowsOcrStatus WindowsOcrReader::probe(const std::string& languageTag) {
    WindowsOcrStatus status;
    if (!prepareThread()) {
        status.message = "Windows OCR must be probed from a worker (MTA) thread.";
        return status;
    }
    (void)createEngine(languageTag, status);
    return status;
}

WinOcrResult WindowsOcrReader::read(const BandImage& image) {
    WinOcrResult result;
    if (!impl_->engine) {
        result.error = impl_->status.message.empty() ? "Windows OCR is not available" : impl_->status.message;
        return result;
    }
    if (image.empty()) return result;
    if (!prepareThread()) {
        result.error = "Windows OCR read() called on a single-threaded apartment (UI) thread";
        return result;
    }
    const uint32_t maxDim = wmo::OcrEngine::MaxImageDimension();
    if (uint32_t(image.width) > maxDim || uint32_t(image.height) > maxDim) {
        result.error = "image larger than OcrEngine::MaxImageDimension";
        return result;
    }
    try {
        const auto t0 = std::chrono::steady_clock::now();
        auto& bmp = impl_->bitmap;
        if (!bmp || impl_->bitmapW != image.width || impl_->bitmapH != image.height) {
            bmp = wgi::SoftwareBitmap(wgi::BitmapPixelFormat::Bgra8, image.width, image.height,
                                      wgi::BitmapAlphaMode::Premultiplied);
            impl_->bitmapW = image.width;
            impl_->bitmapH = image.height;
        }
        {
            // The one copy: straight into the bitmap's own buffer.
            const auto buffer = bmp.LockBuffer(wgi::BitmapBufferAccessMode::Write);
            const auto plane = buffer.GetPlaneDescription(0);
            const auto ref = buffer.CreateReference();
            uint8_t* dst = ref.data() + plane.StartIndex;
            const size_t rowBytes = size_t(image.width) * 4;
            if (plane.Stride == image.stride && size_t(image.stride) == rowBytes) {
                std::memcpy(dst, image.bgra.data(), rowBytes * size_t(image.height));
            } else {
                for (int y = 0; y < image.height; ++y) std::memcpy(dst + size_t(y) * plane.Stride, image.row(y), rowBytes);
            }
        }
        result.copyMs = msSince(t0);
        const auto t1 = std::chrono::steady_clock::now();
        const auto ocr = impl_->engine.RecognizeAsync(bmp).get();
        result.recognizeMs = msSince(t1);
        for (const auto& line : ocr.Lines()) {
            WinOcrLine out;
            out.text = winrt::to_string(line.Text());
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& word : line.Words()) {
                const auto r = word.BoundingRect();
                x0 = std::min(x0, r.X);
                y0 = std::min(y0, r.Y);
                x1 = std::max(x1, r.X + r.Width);
                y1 = std::max(y1, r.Y + r.Height);
            }
            if (x1 < x0) continue;
            out.box = {x0, y0, x1 - x0, y1 - y0};
            result.lines.push_back(std::move(out));
        }
    } catch (const winrt::hresult_error& e) {
        result.error = "Windows OCR failed: " + winrt::to_string(e.message());
        result.lines.clear();
    }
    return result;
}

}  // namespace crispplay::subs::ocr
