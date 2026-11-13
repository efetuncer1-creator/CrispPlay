// CrispPlay subtitle core: a small JSON reader/writer (glossary files, cache lines, test
// fixtures). Std-only. Part of CrispPlay, GPL-3.0 (see LICENSE).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crispplay::subs::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;                                   // UTF-8
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;    // in file order, duplicates kept

    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    bool isNumber() const { return type == Type::Number; }
    // Last member with this name (nullptr if none or not an object).
    const Value* find(std::string_view key) const;
};

// Strict JSON plus, when `lenient`, // and /* */ comments and trailing commas (like
// System.Text.Json with AllowTrailingCommas and CommentHandling.Skip). nullopt on any error.
std::optional<Value> parse(std::string_view text, bool lenient = false);
// JSON string literal with quotes; non-ASCII text is written as UTF-8.
std::string quote(std::string_view utf8);

}  // namespace crispplay::subs::json
