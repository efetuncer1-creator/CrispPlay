#pragma once
// English word list of Windows (Spell Checking API, ISpellChecker "en-US"): tells a real word
// from a partly read one ("kilthosewo", "deerve", "wenally"). The subtitle text rules use it
// through crispplay::subs::OcrNoise::setDictionary.
//
// Threading: create it on a thread in the multithreaded COM apartment (the OCR worker, see
// WindowsOcrReader::prepareThread); knows() may then be called from any thread of the process
// while that apartment exists. Calls are serialised and answers cached.
#include <memory>
#include <string>

namespace crispplay::subs::ocr {

class SpellCheck {
public:
    explicit SpellCheck(const std::wstring& language = L"en-US");
    ~SpellCheck();
    SpellCheck(const SpellCheck&) = delete;
    SpellCheck& operator=(const SpellCheck&) = delete;

    bool ready() const;  // false: no spell checker for the language (knows() then says true)
    // One word (UTF-8, no spaces). Unknown to the word list -> false.
    bool knows(const std::string& word);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crispplay::subs::ocr
