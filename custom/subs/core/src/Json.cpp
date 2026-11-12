// CrispPlay subtitle core: small JSON reader/writer. Part of CrispPlay, GPL-3.0 (see LICENSE).
#include "crispplay/subs/Json.h"

#include "crispplay/subs/Unicode.h"

#include <charconv>
#include <cstdio>

namespace crispplay::subs::json {
namespace {

class Parser {
public:
    Parser(std::string_view s, bool lenient) : s_(s), lenient_(lenient) {}

    std::optional<Value> document() {
        Value v;
        if (!value(v, 0)) return std::nullopt;
        skip();
        if (pos_ != s_.size()) return std::nullopt;
        return v;
    }

private:
    bool skip() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            if (lenient_ && c == '/' && pos_ + 1 < s_.size()) {
                if (s_[pos_ + 1] == '/') {
                    while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
                    continue;
                }
                if (s_[pos_ + 1] == '*') {
                    const size_t end = s_.find("*/", pos_ + 2);
                    if (end == std::string_view::npos) return false;
                    pos_ = end + 2;
                    continue;
                }
            }
            break;
        }
        return true;
    }

    bool literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) return false;
        pos_ += word.size();
        return true;
    }

    bool hex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= c - '0';
            else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= s_.size()) return false;
            const char e = s_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && s_.substr(pos_, 2) == "\\u") {
                    const size_t save = pos_;
                    pos_ += 2;
                    uint32_t low = 0;
                    if (hex4(low) && low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    else pos_ = save;
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                out += uni::encode(static_cast<char32_t>(cp));
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool number(Value& v) {
        const size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                    s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        const auto res = std::from_chars(s_.data() + start, s_.data() + pos_, v.number);
        if (res.ec != std::errc() || res.ptr != s_.data() + pos_) return false;
        v.type = Value::Type::Number;
        return true;
    }

    bool value(Value& v, int depth) {
        if (depth > 64 || !skip() || pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            v.type = Value::Type::Object;
            if (!skip()) return false;
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            while (true) {
                if (!skip()) return false;
                if (lenient_ && pos_ < s_.size() && s_[pos_] == '}' && !v.object.empty()) { ++pos_; return true; }
                std::string key;
                if (!string(key) || !skip() || pos_ >= s_.size() || s_[pos_] != ':') return false;
                ++pos_;
                Value member;
                if (!value(member, depth + 1) || !skip() || pos_ >= s_.size()) return false;
                v.object.emplace_back(std::move(key), std::move(member));
                if (s_[pos_] == ',') { ++pos_; continue; }
                if (s_[pos_] == '}') { ++pos_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            v.type = Value::Type::Array;
            if (!skip()) return false;
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
            while (true) {
                if (!skip()) return false;
                if (lenient_ && pos_ < s_.size() && s_[pos_] == ']' && !v.array.empty()) { ++pos_; return true; }
                Value item;
                if (!value(item, depth + 1) || !skip() || pos_ >= s_.size()) return false;
                v.array.push_back(std::move(item));
                if (s_[pos_] == ',') { ++pos_; continue; }
                if (s_[pos_] == ']') { ++pos_; return true; }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            return string(v.string);
        }
        if (literal("true")) { v.type = Value::Type::Bool; v.boolean = true; return true; }
        if (literal("false")) { v.type = Value::Type::Bool; return true; }
        if (literal("null")) return true;
        return number(v);
    }

    std::string_view s_;
    bool lenient_;
    size_t pos_ = 0;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (auto it = object.rbegin(); it != object.rend(); ++it)
        if (it->first == key) return &it->second;
    return nullptr;
}

std::optional<Value> parse(std::string_view text, bool lenient) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    return Parser(text, lenient).document();
}

std::string quote(std::string_view utf8) {
    std::string out = "\"";
    for (char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    return out + "\"";
}

}  // namespace crispplay::subs::json
