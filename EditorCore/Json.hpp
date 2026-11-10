#pragma once

// Minimal JSON reader / string quoting for the editor's small settings files
// (themes). Strict RFC 8259 syntax (a UTF-8 BOM is skipped), nesting capped,
// \u escapes incl. surrogate pairs decoded to UTF-8. Portable C++, no
// platform code: shared by the Windows editor, tested in EngineCoreTests.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oxo::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object; // file order; a repeated key: the last wins in Find

    bool IsObject() const { return type == Type::Object; }
    bool IsString() const { return type == Type::String; }
    const Value *Find(std::string_view key) const {
        for (auto it = object.rbegin(); it != object.rend(); ++it)
            if (it->first == key) return &it->second;
        return nullptr;
    }
};

namespace detail {
struct Reader {
    std::string_view s;
    size_t p = 0;
    int depth = 0;

    void Space() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    }
    bool Eat(char c) {
        Space();
        if (p < s.size() && s[p] == c) return ++p, true;
        return false;
    }
    bool Literal(std::string_view word) {
        if (s.substr(p, word.size()) != word) return false;
        p += word.size();
        return true;
    }
    static void Utf8(std::string &out, uint32_t c) {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) out += char(0xC0 | c >> 6), out += char(0x80 | (c & 63));
        else if (c < 0x10000) out += char(0xE0 | c >> 12), out += char(0x80 | (c >> 6 & 63)), out += char(0x80 | (c & 63));
        else
            out += char(0xF0 | c >> 18), out += char(0x80 | (c >> 12 & 63)), out += char(0x80 | (c >> 6 & 63)),
                out += char(0x80 | (c & 63));
    }
    bool Hex4(uint32_t &v) {
        if (p + 4 > s.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = s[p++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= uint32_t(h - '0');
            else if (h >= 'a' && h <= 'f') v |= uint32_t(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= uint32_t(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool String(std::string &out) {
        out.clear();
        if (!Eat('"')) return false;
        while (p < s.size()) {
            const char c = s[p++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= s.size()) return false;
            switch (s[p++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t c1 = 0;
                if (!Hex4(c1)) return false;
                if (c1 >= 0xD800 && c1 < 0xDC00) {
                    uint32_t c2 = 0;
                    if (!Literal("\\u") || !Hex4(c2) || c2 < 0xDC00 || c2 >= 0xE000) return false;
                    c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                } else if (c1 >= 0xDC00 && c1 < 0xE000) {
                    return false;
                }
                Utf8(out, c1);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Number(double &out) {
        const size_t start = p;
        if (p < s.size() && s[p] == '-') ++p;
        if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
        if (s[p] == '0') ++p;
        else while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (p < s.size() && s[p] == '.') {
            if (++p >= s.size() || s[p] < '0' || s[p] > '9') return false;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            if (++p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
            if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        }
        out = std::strtod(std::string(s.substr(start, p - start)).c_str(), nullptr);
        return true;
    }
    bool Parse(Value &v) {
        Space();
        if (p >= s.size() || depth > 64) return false;
        const char c = s[p];
        if (c == '"') return v.type = Value::Type::String, String(v.string);
        if (c == '{' || c == '[') {
            ++depth;
            const bool ok = c == '{' ? Object(v) : Array(v);
            --depth;
            return ok;
        }
        if (Literal("true")) return v.type = Value::Type::Bool, v.boolean = true, true;
        if (Literal("false")) return v.type = Value::Type::Bool, true;
        if (Literal("null")) return v.type = Value::Type::Null, true;
        return v.type = Value::Type::Number, Number(v.number);
    }
    bool Object(Value &v) {
        v.type = Value::Type::Object;
        ++p;
        if (Eat('}')) return true;
        do {
            std::pair<std::string, Value> item;
            if (!String(item.first) || !Eat(':') || !Parse(item.second)) return false;
            v.object.push_back(std::move(item));
        } while (Eat(','));
        return Eat('}');
    }
    bool Array(Value &v) {
        v.type = Value::Type::Array;
        ++p;
        if (Eat(']')) return true;
        do {
            v.array.emplace_back();
            if (!Parse(v.array.back())) return false;
        } while (Eat(','));
        return Eat(']');
    }
};
} // namespace detail

// false = not one complete JSON value (out is then unspecified).
inline bool Parse(std::string_view text, Value &out) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    detail::Reader r{text};
    out = {};
    if (!r.Parse(out)) return false;
    r.Space();
    return r.p == text.size();
}

// A JSON string literal: quotes, backslashes and control characters escaped.
inline std::string Quote(std::string_view s) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string o = "\"";
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (u < 0x20) o += "\\u00", o += hex[u >> 4], o += hex[u & 15];
        else o += c;
    }
    return o + '"';
}

} // namespace oxo::json
