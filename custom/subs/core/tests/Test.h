// CrispPlay subtitle core: a tiny self-contained test runner (no Qt needed).
// Part of CrispPlay, GPL-3.0 (see LICENSE).
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace subtest {

struct Case { const char* name; std::function<void()> body; };
std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& message);
// Skipped tests count separately (e.g. replay data missing on this machine).
void skip(const std::string& reason);
struct Registrar { Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); } };

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
    else if constexpr (std::is_same_v<T, std::u32string>) {
        for (char32_t c : value) out << (c < 0x80 ? std::string(1, static_cast<char>(c)) : "U+" + std::to_string(c));
    } else if constexpr (requires { out << value; }) out << value;
    else out << "<value>";
    return out.str();
}
inline std::string show(const std::string& value) { return "\"" + value + "\""; }
inline std::string show(const char* value) { return value ? "\"" + std::string(value) + "\"" : "null"; }
template <typename T>
std::string show(const std::vector<T>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + show(values[i]);
    return out + "]";
}

}  // namespace subtest

#define SUBTEST_CAT2(a, b) a##b
#define SUBTEST_CAT(a, b) SUBTEST_CAT2(a, b)
#define TEST(name)                                                                              \
    static void name();                                                                         \
    static subtest::Registrar SUBTEST_CAT(registrar_, name)(#name, name);                       \
    static void name()

#define CHECK(cond)                                                                             \
    do { if (!(cond)) subtest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(actual, expected)                                                              \
    do {                                                                                        \
        const auto& a_ = (actual);                                                              \
        const auto& e_ = (expected);                                                            \
        if (!(a_ == e_))                                                                        \
            subtest::fail(__FILE__, __LINE__, std::string(#actual " == " #expected "\n    actual:   ") + \
                          subtest::show(a_) + "\n    expected: " + subtest::show(e_));          \
    } while (0)
