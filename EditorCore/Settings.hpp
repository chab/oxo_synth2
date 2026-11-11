#pragma once

// The per-user settings.json in storage::Root() (portable Synth2_Data and
// standard installs stay independent): one JSON object of independent
// top-level keys with plain values (number / true / false). A save edits
// only its key's value in place, every other byte kept (keys of other
// versions too); an unreadable file is never replaced. Never VST3 state.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace oxo::settings {
namespace fs = std::filesystem;

inline constexpr const char *kFileName = "settings.json";

struct Field {
    bool found = false;
    bool valid = false; // a plain value token
    size_t begin = 0;
    size_t end = 0;
};

inline bool IsWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
inline bool IsTokenChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '+' ||
           c == '-';
}

// Find only a TOP-LEVEL JSON key, never a similarly named key in a nested
// object or quoted string. The file is expected to contain a JSON object.
inline Field Find(const std::string &json, std::string_view key) {
    int depth = 0;
    for (size_t i = 0; i < json.size();) {
        const char c = json[i];
        if (c == '"') {
            const size_t start = ++i;
            bool escaped = false;
            while (i < json.size()) {
                const char ch = json[i++];
                if (escaped) { escaped = false; continue; }
                if (ch == '\\') { escaped = true; continue; }
                if (ch == '"') break;
            }
            if (i > json.size() || json[i - 1] != '"') return {};
            if (depth != 1 || i - start - 1 != key.size() || json.compare(start, key.size(), key.data(), key.size()) != 0)
                continue;
            size_t at = i;
            while (at < json.size() && IsWhitespace(json[at])) ++at;
            if (at >= json.size() || json[at] != ':') continue; // not a property key
            ++at;
            while (at < json.size() && IsWhitespace(json[at])) ++at;
            Field found{true, false, at, at};
            while (found.end < json.size() && IsTokenChar(json[found.end])) ++found.end;
            found.valid = found.end > found.begin &&
                          (found.end == json.size() || IsWhitespace(json[found.end]) || json[found.end] == ',' ||
                           json[found.end] == '}');
            return found;
        }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
        if (depth < 0) return {};
        ++i;
    }
    return {};
}

inline bool ReadFile(const fs::path &path, std::string &contents) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec || !fs::is_regular_file(path, ec) || ec || fs::file_size(path, ec) > 1024 * 1024 ||
        ec)
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    contents.assign(std::istreambuf_iterator<char>(file), {});
    return !file.bad();
}

// The value token of `key` ("700", "true"), nullopt = no file / no such key
// / not a plain value.
inline std::optional<std::string> Load(const fs::path &root, std::string_view key) {
    std::string json;
    if (root.empty() || !ReadFile(root / kFileName, json)) return std::nullopt;
    const Field field = Find(json, key);
    if (!field.found || !field.valid) return std::nullopt;
    return json.substr(field.begin, field.end - field.begin);
}

// `token` = the JSON value as written ("700", "true").
inline bool Save(const fs::path &root, std::string_view key, const std::string &token) {
    if (root.empty() || token.empty()) return false;
    const fs::path target = root / kFileName;
    std::string json;
    std::error_code ec;
    const bool existing = fs::exists(target, ec);
    if (existing && !ReadFile(target, json)) return false; // never overwrite unreadable data
    const std::string entry = "  \"" + std::string(key) + "\": " + token;
    if (!existing) {
        json = "{\n" + entry + "\n}\n";
    } else {
        const Field field = Find(json, key);
        if (field.found) {
            if (!field.valid) return false; // unknown setting type; preserve it
            json.replace(field.begin, field.end - field.begin, token);
        } else {
            const size_t first = json.find_first_not_of(" \t\n\r");
            const size_t last = json.find_last_not_of(" \t\n\r");
            if (first == std::string::npos || last == std::string::npos || json[first] != '{' || json[last] != '}')
                return false;
            const size_t lastValue = json.find_last_not_of(" \t\n\r", last - 1); // >= first
            if (lastValue == first) json.insert(last, "\n" + entry + "\n"); // {}
            else json.insert(lastValue + 1, ",\n" + entry);
        }
    }
    fs::create_directories(root, ec);
    if (ec) return false;
    fs::path temp = target;
#ifdef _WIN32
    temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId());
#else
    temp += ".tmp";
#endif
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.flush();
        if (!out) {
            fs::remove(temp, ec);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp, ec);
        return false;
    }
#else
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
#endif
    return true;
}

inline bool LoadBool(const fs::path &root, std::string_view key, bool fallback) {
    const std::optional<std::string> v = Load(root, key);
    return v == "true" ? true : v == "false" ? false : fallback;
}
inline bool SaveBool(const fs::path &root, std::string_view key, bool value) {
    return Save(root, key, value ? "true" : "false");
}

} // namespace oxo::settings
