#pragma once
// First reader and line finder: Windows.Media.Ocr through C++/WinRT.
//
// Threading: construct and call read() on a worker thread. A thread without
// COM is put into the multithreaded apartment; a single-threaded apartment
// (the Qt UI thread is one) is refused with an error instead of blocking it.
#include <memory>
#include <string>
#include <vector>

#include "crispplay/subs/ocr/BandImage.h"
#include "crispplay/subs/ocr/OcrTypes.h"

namespace crispplay::subs::ocr {

struct WindowsOcrStatus {
    bool available = false;          // an engine could be created
    bool english = false;            // ... and it reads English
    std::string languageTag;         // language of the engine in use, e.g. "en-US"
    std::vector<std::string> installedLanguages;  // OcrEngine::AvailableRecognizerLanguages
    std::string message;             // human-readable reason when !available or !english
};

struct WinOcrLine {
    std::string text;  // UTF-8, words joined by single spaces
    Rect box;          // union of the word boxes, pixels of the image given to read()
};

struct WinOcrResult {
    std::vector<WinOcrLine> lines;
    double copyMs = 0;       // BandImage -> SoftwareBitmap
    double recognizeMs = 0;  // RecognizeAsync().get()
    std::string error;       // empty on success
};

class WindowsOcrReader {
public:
    // Tries `languageTag` first, then the user's profile languages.
    explicit WindowsOcrReader(const std::string& languageTag = "en-US");
    ~WindowsOcrReader();
    WindowsOcrReader(const WindowsOcrReader&) = delete;
    WindowsOcrReader& operator=(const WindowsOcrReader&) = delete;

    const WindowsOcrStatus& status() const;
    bool ready() const { return status().available; }

    WinOcrResult read(const BandImage& image);

    // What the settings UI needs to warn about a missing English OCR pack,
    // without keeping an engine around.
    static WindowsOcrStatus probe(const std::string& languageTag = "en-US");

    // Puts the calling thread into the MTA if it has no apartment yet; false
    // when it is a single-threaded apartment (UI thread).
    static bool prepareThread();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crispplay::subs::ocr
