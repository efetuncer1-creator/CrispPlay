// CrispPlay subtitle core: test runner entry point. Part of CrispPlay, GPL-3.0 (see LICENSE).
// Usage: crispplay_subs_core_tests [name-substring]
#include "Test.h"

#include <cstdio>
#include <cstring>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

namespace subtest {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

static int failures = 0;
static bool currentFailed = false;
static bool currentSkipped = false;

void fail(const char* file, int line, const std::string& message) {
    const char* base = std::strrchr(file, '\\');
    if (!base) base = std::strrchr(file, '/');
    std::printf("  FAIL %s:%d: %s\n", base ? base + 1 : file, line, message.c_str());
    currentFailed = true;
}

void skip(const std::string& reason) {
    std::printf("  SKIP %s\n", reason.c_str());
    currentSkipped = true;
}

}  // namespace subtest

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    int passed = 0, failed = 0, skipped = 0;
    for (auto& test : subtest::registry()) {
        if (argc > 1 && !std::strstr(test.name, argv[1])) continue;
        subtest::currentFailed = false;
        subtest::currentSkipped = false;
        try {
            test.body();
        } catch (const std::exception& e) {
            subtest::fail(test.name, 0, std::string("exception: ") + e.what());
        }
        std::fflush(stdout);
        if (subtest::currentFailed) {
            ++failed;
            std::printf("FAILED %s\n", test.name);
        } else if (subtest::currentSkipped) {
            ++skipped;
        } else {
            ++passed;
        }
    }
    std::fflush(stdout);
    std::printf("Totals: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}
