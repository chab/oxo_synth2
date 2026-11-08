#pragma once

// Windows editor zoom preference. Uses the existing per-user settings.json
// in storage::Root() so portable Synth2_Data and standard installs stay
// independent. Never replaces other JSON settings or modifies VST3 state.

#include <charconv>
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

namespace oxo::winzoom {
namespace fs = std::filesystem;
constexpr std::string_view kKey = "windowsEditorWidth";

struct NumberField {
    bool found = false;
    bool valid = false;
    size_t begin = 0;
    size_t end = 0;
};

inline bool IsWhitespace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

// Find only a TOP-LEVEL JSON key, never a similarly named key in a nested
// object or quoted string. The file is expected to contain a JSON object.
inline NumberField FindWidthField(const std::string& json) {
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
            if (depth != 1 || json.compare(start, i - start - 1, kKey.data(), kKey.size()) != 0 ||
                i - start - 1 != kKey.size()) continue;
            size_t at = i;
            while (at < json.size() && IsWhitespace(json[at])) ++at;
            if (at >= json.size() || json[at] != ':') continue; // not a property key
            ++at;
            while (at < json.size() && IsWhitespace(json[at])) ++at;
            NumberField found{true, false, at, at};
            while (found.end < json.size() && json[found.end] >= '0' && json[found.end] <= '9') ++found.end;
            found.valid = found.end > found.begin &&
                (found.end == json.size() || IsWhitespace(json[found.end]) ||
                 json[found.end] == ',' || json[found.end] == '}');
            return found;
        }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
        if (depth < 0) return {};
        ++i;
    }
    return {};
}

inline bool ReadSettings(const fs::path& path, std::string& contents) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec || !fs::is_regular_file(path, ec) || ec ||
        fs::file_size(path, ec) > 1024 * 1024 || ec) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    contents.assign(std::istreambuf_iterator<char>(file), {});
    return !file.bad();
}

inline int LoadWidth(const fs::path& root, int fallback, int minimum, int maximum) {
    std::string json;
    if (!ReadSettings(root / "settings.json", json)) return fallback;
    const NumberField field = FindWidthField(json);
    if (!field.found || !field.valid) return fallback;
    int width = 0;
    const auto result = std::from_chars(json.data() + field.begin, json.data() + field.end, width);
    if (result.ec != std::errc{} || result.ptr != json.data() + field.end ||
        width < minimum || width > maximum) return fallback;
    return width;
}

inline bool SaveWidth(const fs::path& root, int width, int minimum, int maximum) {
    if (root.empty() || width < minimum || width > maximum) return false;
    const fs::path target = root / "settings.json";
    std::string json;
    const bool existing = fs::exists(target);
    if (existing && !ReadSettings(target, json)) return false; // never overwrite unreadable data
    const std::string number = std::to_string(width);
    if (!existing) {
        json = "{\n  \"windowsEditorWidth\": " + number + "\n}\n";
    } else {
        const NumberField field = FindWidthField(json);
        if (field.found) {
            if (!field.valid) return false; // unknown setting type; preserve it
            json.replace(field.begin, field.end - field.begin, number);
        } else {
            const size_t first = json.find_first_not_of(" \t\n\r");
            const size_t last = json.find_last_not_of(" \t\n\r");
            if (first == std::string::npos || last == std::string::npos ||
                json[first] != '{' || json[last] != '}') return false;
            size_t contentsStart = first + 1;
            while (contentsStart < last && IsWhitespace(json[contentsStart])) ++contentsStart;
            const bool hasFields = contentsStart < last;
            json.insert(last, std::string(hasFields ? ",\n" : "\n") +
                "  \"windowsEditorWidth\": " + number + "\n");
        }
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;
    fs::path temp = target;
#ifdef _WIN32
    temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetCurrentThreadId());
#else
    temp += ".tmp";
#endif
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.flush();
        if (!out) { fs::remove(temp, ec); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp, ec);
        return false;
    }
#else
    fs::rename(temp, target, ec);
    if (ec) { fs::remove(temp, ec); return false; }
#endif
    return true;
}
} // namespace oxo::winzoom
