#include "crispplay/subs/ocr/SpellCheck.h"

#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>

#include <mutex>
#include <unordered_map>

namespace crispplay::subs::ocr {
namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}

}  // namespace

struct SpellCheck::Impl {
    ISpellChecker* checker = nullptr;
    std::mutex mutex;
    std::unordered_map<std::string, bool> cache;
};

SpellCheck::SpellCheck(const std::wstring& language) : impl_(std::make_unique<Impl>()) {
    ISpellCheckerFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory)
        return;
    BOOL supported = FALSE;
    if (SUCCEEDED(factory->IsSupported(language.c_str(), &supported)) && supported)
        factory->CreateSpellChecker(language.c_str(), &impl_->checker);
    factory->Release();
}

SpellCheck::~SpellCheck() {
    if (impl_->checker) impl_->checker->Release();
}

bool SpellCheck::ready() const { return impl_->checker != nullptr; }

bool SpellCheck::knows(const std::string& word) {
    if (!impl_->checker || word.empty()) return true;
    std::lock_guard lock(impl_->mutex);
    if (auto it = impl_->cache.find(word); it != impl_->cache.end()) return it->second;
    bool known = true;
    IEnumSpellingError* errors = nullptr;
    if (SUCCEEDED(impl_->checker->Check(widen(word).c_str(), &errors)) && errors) {
        ISpellingError* error = nullptr;
        known = errors->Next(&error) != S_OK;
        if (error) error->Release();
        errors->Release();
    }
    if (impl_->cache.size() > 20000) impl_->cache.clear();
    impl_->cache.emplace(word, known);
    return known;
}

}  // namespace crispplay::subs::ocr
