// CrispPlay subtitle core: SHA-256 (FIPS 180-4) for short keys such as cache file names.
// Std-only. Part of CrispPlay, GPL-3.0 (see LICENSE).
#pragma once

#include <string>
#include <string_view>

namespace crispplay::subs {

// Lower-case hex digest of the bytes.
std::string sha256Hex(std::string_view bytes);

}  // namespace crispplay::subs
