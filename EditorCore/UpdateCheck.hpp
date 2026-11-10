#pragma once

// Menu "Check for updates…" for the C++ editors (Windows): the C++ mirror of
// Shared/SynthUpdateCheck.swift's UpdateCheck.evaluate / message (same
// rules, same texts; EngineCoreTests/UpdateCheckTests.cpp checks the
// texts against the Swift file). The answer comes from
// `<vendor site>/releases.json` (the oxo-music.com site's
// config/releases.yml): { products: { <slug>: { page, mac: { version,
// build, url }, win: … } } }. Newer = a higher BUILD for this platform
// (always increasing, never reset), the version is only shown. No entry
// for the product / platform = up to date. Portable, no networking here.

#include "Json.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace oxo::update {

enum class Kind { Checking, Latest, Available, Unavailable };

struct State {
    Kind kind = Kind::Unavailable;
    std::string latest;    // Available: "V1.2" (+ " (build N)" when the versions match)
    std::string installed; // Latest / Available: "V1.1"
    std::string page;      // Available: the download page
};

// "V1.31", plus the build when the other side has the same version.
inline std::string Label(const std::string &version, int build, const std::string &other) {
    return "V" + version + (version == other ? " (build " + std::to_string(build) + ")" : "");
}

// `site` = "https://oxo-music.com". Not our JSON (an HTML error page, …) =
// Unavailable.
inline State Evaluate(std::string_view text, const std::string &product, const std::string &platform,
                      const std::string &version, int build, const std::string &site) {
    json::Value root;
    const json::Value *products = json::Parse(text, root) && root.IsObject() ? root.Find("products") : nullptr;
    if (!products || !products->IsObject()) return {};
    const json::Value *entry = products->Find(product);
    const json::Value *release = entry && entry->IsObject() ? entry->Find(platform) : nullptr;
    const json::Value *latestVersion = release && release->IsObject() ? release->Find("version") : nullptr;
    const json::Value *latestBuild = release && release->IsObject() ? release->Find("build") : nullptr;
    // Swift: `as? String`, `as? Int`
    if (!latestVersion || !latestVersion->IsString() || !latestBuild ||
        latestBuild->type != json::Value::Type::Number || latestBuild->number != std::floor(latestBuild->number) ||
        latestBuild->number <= double(build))
        return {Kind::Latest, {}, "V" + version, {}};
    const json::Value *page = entry->Find("page");
    const int newBuild = int(latestBuild->number);
    return {Kind::Available, Label(latestVersion->string, newBuild, version),
            Label(version, build, latestVersion->string),
            page && page->IsString() ? page->string : site + "/products/" + product};
}

// `siteName` = "oxo-music.com", `email` without "mailto:".
inline std::string Message(const State &s, const std::string &siteName, const std::string &email) {
    switch (s.kind) {
    case Kind::Checking: return "Checking " + siteName + "\xE2\x80\xA6";
    case Kind::Latest: return "You're using the latest version (" + s.installed + ").";
    case Kind::Available:
        return "A new version " + s.latest + " is available on " + s.page + "\n\nYour version is " + s.installed + ".";
    case Kind::Unavailable: break;
    }
    return siteName + " is not available. Please try again later, or contact " + email +
           " if the problem persists.";
}

// "https://oxo-music.com" -> "oxo-music.com"; "mailto:a@b" -> "a@b".
inline std::string SiteName(std::string site) {
    if (const auto p = site.find("://"); p != std::string::npos) site.erase(0, p + 3);
    if (const auto p = site.find('/'); p != std::string::npos) site.erase(p);
    return site;
}
inline std::string Email(std::string email) {
    if (email.rfind("mailto:", 0) == 0) email.erase(0, 7);
    return email;
}

} // namespace oxo::update
