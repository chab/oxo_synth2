#pragma once

// Panel themes for C++ editors (the Windows one): the C++ mirror of
// Shared/SynthTheme.swift, so a theme means the same on mac, iPad and
// Windows. Kept in lockstep by EngineCoreTests/ThemesTests.cpp, which parses
// the Swift file: same 13 keys in the same order, same factory themes.
//  - A palette = the 13 ThemeColor keys; everything else on the panel is
//    derived from them or fixed.
//  - Theme data = a flat JSON object {"panel": "#B7B79B", ...}
//    (ThemePalette.hexMap), read leniently: unknown keys ignored, a missing
//    or invalid key = the Synth2 default.
//  - The session text (SynthHost.themeText) = that map, compact, keys
//    sorted (ThemePalette.stateText).
//  - Store = ThemeStore: <data folder>/themes.json, {"current": map,
//    "saved": [{"name": ..., "colors": map}]}; current = the palette a NEW
//    instance starts with (the last one picked / edited in any instance).

#include "Json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oxo::themes {

// ThemeColor's cases, in order.
enum Slot { kPanel, kHeader, kHeaderText, kLabel, kKnobBody, kKnobGradient, kKnobDial, kButton, kLed, kRadio,
            kDisplayText, kDisplayTop, kDisplayBottom, kSlotCount };
inline constexpr std::array<const char *, kSlotCount> kKeys = {
    "panel", "header", "headerText", "label", "knobBody", "knobGradient", "knobDial",
    "button", "led", "radio", "displayText", "displayTop", "displayBottom"};
// ThemeColor.title (the theme editor's row names).
inline constexpr std::array<const char *, kSlotCount> kTitles = {
    "Panel", "Section header", "Header text", "Labels", "Knob body", "Knob gradient", "Knob dial",
    "Buttons", "Button LED", "Radio LED", "Display text", "Display top", "Display bottom"};

using Palette = std::array<uint32_t, kSlotCount>; // 0xRRGGBB per slot

struct Named {
    std::string name;
    Palette palette;
};

// ThemePalette.factory, in order ("Synth2" = the default).
inline const std::vector<Named> &Factory() {
    static const std::vector<Named> factory = {
        {"Synth2", {0xB7B79B, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A}},
        {"Blue", {0x8AA7B8, 0x1A1D26, 0x9DA198, 0x181A19, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xA4DE87, 0x73F2FF, 0x24272F, 0x1C1F26}},
        {"Gray Dark", {0x3F434D, 0x5F5F60, 0xD1D5D7, 0xE4E7EC, 0x100F0F, 0x151516, 0xB5B6B9, 0x363630, 0x8ED9B5, 0xF3F9F1, 0xC2D3E1, 0x161A1A, 0x0D100C}},
        {"Gray Light", {0xCECEC0, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A}},
        {"Red", {0xA71818, 0x474747, 0xFFFFFF, 0xFFFFFF, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xA4DE87, 0x73F2FF, 0x24272F, 0x1C1F26}},
        {"Warm", {0x313336, 0xB78F0F, 0x000000, 0xE4E7EC, 0x1B1B1B, 0x181818, 0xB5B6B9, 0x363630, 0x8ED9B5, 0xF3F9F1, 0xC2D3E1, 0x161A1A, 0x0D100C}},
        {"JP-8000", {0x304354, 0x24262C, 0xC6772D, 0xE4E7EC, 0x1B1B1B, 0x181818, 0xB5B6B9, 0x363630, 0xFAFFF2, 0xF5FBF3, 0xC2D3E1, 0x161A1A, 0x0D100C}},
        {"JP-8000 Dark", {0x191D25, 0x24262C, 0xC6772D, 0xE4E7EC, 0x1B1B1B, 0x181818, 0xB5B6B9, 0x363630, 0xFAFFF2, 0xF5FBF3, 0xC2D3E1, 0x161A1A, 0x0D100C}},
        {"Synth1", {0xC7C786, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFF2D2D, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A}},
    };
    return factory;
}
inline const Palette &Synth2() { return Factory().front().palette; }

// RGB(hex:): '#' / spaces around, then exactly 6 hex digits.
inline bool ParseHex(std::string_view text, uint32_t &rgb) {
    while (!text.empty() && (text.front() == '#' || text.front() == ' ')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == '#' || text.back() == ' ')) text.remove_suffix(1);
    if (text.size() != 6) return false;
    uint32_t v = 0;
    for (const char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        v = v << 4 | uint32_t(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    rgb = v;
    return true;
}
inline std::string Hex(uint32_t rgb) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string s = "#";
    for (int shift = 20; shift >= 0; shift -= 4) s += digits[rgb >> shift & 15];
    return s;
}

// ThemePalette(hexMap:): Synth2 defaults, then every known key with a valid
// colour; anything else is ignored.
inline Palette FromHexMap(const json::Value &map) {
    Palette p = Synth2();
    for (const auto &[key, value] : map.object) {
        const auto slot = std::find_if(kKeys.begin(), kKeys.end(), [&](const char *k) { return key == k; });
        uint32_t rgb = 0;
        if (slot != kKeys.end() && value.IsString() && ParseHex(value.string, rgb)) p[size_t(slot - kKeys.begin())] = rgb;
    }
    return p;
}

// ThemePalette.hexMap as JSON: compact, keys sorted (= stateText).
inline std::string HexMapJson(const Palette &p) {
    std::map<std::string, std::string> sorted;
    for (size_t i = 0; i < kSlotCount; ++i) sorted[kKeys[i]] = Hex(p[i]);
    std::string j = "{";
    for (const auto &[key, hex] : sorted) j += (j.size() > 1 ? "," : "") + json::Quote(key) + ":" + json::Quote(hex);
    return j + "}";
}
inline std::string StateText(const Palette &p) { return HexMapJson(p); }

// ThemePalette(stateText:): nullopt = empty / not a JSON object (the
// session then keeps the editor's palette).
inline std::optional<Palette> FromStateText(std::string_view text) {
    json::Value v;
    if (!json::Parse(text, v) || !v.IsObject()) return std::nullopt;
    return FromHexMap(v);
}

// ThemeStore. Not thread-safe: one per process, used from the UI thread.
class Store {
public:
    explicit Store(std::filesystem::path file) : file_(std::move(file)) {}

    const Palette &Current() const { return current_; }
    const std::vector<Named> &Saved() const { return saved_; }
    const std::filesystem::path &File() const { return file_; }

    // Saved themes not identical to a factory theme of the same name.
    std::vector<Named> UserThemes() const {
        std::vector<Named> out;
        for (const auto &s : saved_)
            if (std::none_of(Factory().begin(), Factory().end(),
                             [&](const Named &f) { return f.name == s.name && f.palette == s.palette; }))
                out.push_back(s);
        return out;
    }

    // Re-reads the file (another process may have written it). A missing or
    // unreadable file keeps what is in memory, like ThemeStore.load().
    bool Load() {
        std::ifstream f(file_, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        json::Value v;
        if (!json::Parse(ss.str(), v) || !v.IsObject()) return false;
        const json::Value *current = v.Find("current"), *saved = v.Find("saved");
        if (!current || !current->IsObject() || !saved || saved->type != json::Value::Type::Array) return false;
        std::vector<Named> list;
        for (const auto &item : saved->array) {
            const json::Value *name = item.Find("name"), *colors = item.Find("colors");
            if (!item.IsObject() || !name || !name->IsString() || !colors || !colors->IsObject()) return false;
            list.push_back({name->string, FromHexMap(*colors)});
        }
        current_ = FromHexMap(*current);
        saved_ = std::move(list);
        return true;
    }

    // ThemeStore.remember: the default for the next instances. true = it
    // changed (the caller then writes, once the edit settles).
    bool Remember(const Palette &p) {
        if (current_ == p) return false;
        current_ = p;
        return true;
    }

    // ThemeStore.save: trimmed name, same name = replaced, sorted by name
    // (case-insensitive). Writes the file.
    bool Save(const Palette &p, std::string name) {
        const auto first = name.find_first_not_of(" \t"), last = name.find_last_not_of(" \t");
        if (first == std::string::npos) return false;
        name = name.substr(first, last - first + 1);
        Delete(name, false);
        saved_.push_back({name, p});
        std::stable_sort(saved_.begin(), saved_.end(), [](const Named &a, const Named &b) {
            return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                                [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
        });
        return Write();
    }
    bool Delete(const std::string &name, bool write = true) {
        saved_.erase(std::remove_if(saved_.begin(), saved_.end(), [&](const Named &s) { return s.name == name; }),
                     saved_.end());
        return !write || Write();
    }

    // The whole file, atomically (temp file + rename).
    bool Write() const {
        std::string j = "{\"current\":" + HexMapJson(current_) + ",\"saved\":[";
        for (size_t i = 0; i < saved_.size(); ++i)
            j += (i ? "," : "") + std::string("{\"name\":") + json::Quote(saved_[i].name) +
                 ",\"colors\":" + HexMapJson(saved_[i].palette) + "}";
        j += "]}";
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        const std::filesystem::path tmp = file_.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!(f << j) || !f.flush()) return false;
        }
        std::filesystem::rename(tmp, file_, ec);
        if (!ec) return true;
        std::filesystem::remove(tmp, ec);
        return false;
    }

private:
    std::filesystem::path file_;
    Palette current_ = Synth2();
    std::vector<Named> saved_;
};

} // namespace oxo::themes
