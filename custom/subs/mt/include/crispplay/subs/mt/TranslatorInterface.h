#pragma once

// The Translator interface is owned by the subtitle core
// (custom/subs/core/include/crispplay/subs/Types.h). Until that branch is merged
// this header carries an identical local copy; once Types.h is on the include
// path the copy below is skipped, so there is only ever one definition.

#if __has_include(<crispplay/subs/Types.h>)
#include <crispplay/subs/Types.h>
#else
#include <optional>
#include <string>

namespace crispplay::subs {

class Translator {
public:
    virtual ~Translator() = default;
    virtual std::optional<std::string> translate(const std::string& english) = 0; // blocking, worker thread
    virtual void cancel() {}
};

} // namespace crispplay::subs
#endif
