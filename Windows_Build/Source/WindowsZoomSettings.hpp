#pragma once

// Windows editor zoom preference: key "windowsEditorWidth" of the per-user
// settings.json (EditorCore/Settings.hpp).

#include "EditorCore/Settings.hpp"

#include <charconv>
#include <string>
#include <string_view>

namespace oxo::winzoom {
namespace fs = std::filesystem;
constexpr std::string_view kKey = "windowsEditorWidth";

inline int LoadWidth(const fs::path& root, int fallback, int minimum, int maximum) {
    const std::optional<std::string> token = settings::Load(root, kKey);
    if (!token) return fallback;
    int width = 0;
    const char *end = token->data() + token->size();
    const auto result = std::from_chars(token->data(), end, width);
    if (result.ec != std::errc{} || result.ptr != end || (*token)[0] < '0' || (*token)[0] > '9' ||
        width < minimum || width > maximum) return fallback;
    return width;
}

inline bool SaveWidth(const fs::path& root, int width, int minimum, int maximum) {
    if (width < minimum || width > maximum) return false;
    return settings::Save(root, kKey, std::to_string(width));
}
} // namespace oxo::winzoom
