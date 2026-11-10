#include "TestFramework.hpp"

#include "../EditorCore/Themes.hpp"

#include <fstream>
#include <regex>
#include <sstream>

using namespace oxo;
namespace fs = std::filesystem;

namespace {
std::string ReadText(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

fs::path FreshDir(const char *name) {
    const fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

std::string Swift() {
    std::string s = ReadText(fs::path(OXO_TESTS_DIR) / ".." / "Shared" / "SynthTheme.swift");
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}
} // namespace

// EditorCore/Themes.hpp must say what Shared/SynthTheme.swift says.
OXO_TEST(ThemeKeysMatchSwift) {
    const std::string s = Swift();
    std::smatch m;
    OXO_EXPECT(std::regex_search(s, m, std::regex(R"(enum ThemeColor[^{]*\{\s*case ([^v]*?)\s+var title)")));
    std::vector<std::string> keys;
    const std::string list = m[1];
    const std::regex word(R"(\w+)");
    for (std::sregex_iterator it(list.begin(), list.end(), word), end; it != end; ++it)
        keys.push_back(it->str());
    OXO_EXPECT_EQ(keys.size(), size_t(themes::kSlotCount));
    for (size_t i = 0; i < keys.size() && i < themes::kSlotCount; ++i) {
        OXO_EXPECT_EQ(keys[i], std::string(themes::kKeys[i]));
        std::smatch t;
        OXO_EXPECT(std::regex_search(s, t, std::regex("case \\." + keys[i] + ": return \"([^\"]+)\"")));
        OXO_EXPECT_EQ(t[1].str(), std::string(themes::kTitles[i]));
    }
}

OXO_TEST(FactoryThemesMatchSwift) {
    const std::string s = Swift();
    std::vector<themes::Named> swift;
    // "Synth2" = .synth2 = ThemeColor.defaultHex.
    themes::Palette synth2{};
    for (size_t i = 0; i < themes::kSlotCount; ++i) {
        std::smatch m;
        const std::string key = themes::kKeys[i];
        OXO_EXPECT(std::regex_search(s, m, std::regex("var defaultHex.*?case \\." + key + ": return \"(#[0-9A-F]{6})\"")));
        OXO_EXPECT(themes::ParseHex(m[1].str(), synth2[i]));
    }
    swift.push_back({"Synth2", synth2});
    const std::regex entry(R"re(\("([^"]+)", ThemePalette\(hexMap: \[([^\]]*)\]\)\))re");
    const std::regex pair(R"re("(\w+)": "(#[0-9A-Fa-f]{6})")re");
    for (std::sregex_iterator it(s.begin(), s.end(), entry), end; it != end; ++it) {
        const std::string map = (*it)[2];
        json::Value v;
        v.type = json::Value::Type::Object;
        for (std::sregex_iterator p(map.begin(), map.end(), pair); p != end; ++p) {
            json::Value hex;
            hex.type = json::Value::Type::String;
            hex.string = (*p)[2];
            v.object.push_back({(*p)[1], hex});
        }
        OXO_EXPECT_EQ(v.object.size(), size_t(themes::kSlotCount)); // every factory theme sets all 13
        swift.push_back({(*it)[1], themes::FromHexMap(v)});
    }
    OXO_EXPECT_EQ(swift.size(), themes::Factory().size());
    for (size_t i = 0; i < swift.size() && i < themes::Factory().size(); ++i) {
        OXO_EXPECT_EQ(swift[i].name, themes::Factory()[i].name);
        OXO_EXPECT(swift[i].palette == themes::Factory()[i].palette);
    }
}

OXO_TEST(ThemeHexMapIsLenient) {
    // Unknown keys ignored, invalid / missing = the Synth2 default, '#' and
    // case optional (RGB(hex:)).
    const auto p = themes::FromStateText(
        R"({"panel":"#010203","label":"a0b1c2","led":"#12345","voiceGridBorder":"#FFFFFF","radio":7})");
    OXO_EXPECT(p.has_value());
    OXO_EXPECT_EQ((*p)[themes::kPanel], 0x010203u);
    OXO_EXPECT_EQ((*p)[themes::kLabel], 0xA0B1C2u);
    OXO_EXPECT_EQ((*p)[themes::kLed], themes::Synth2()[themes::kLed]);
    OXO_EXPECT_EQ((*p)[themes::kRadio], themes::Synth2()[themes::kRadio]);
    OXO_EXPECT_EQ((*p)[themes::kHeader], themes::Synth2()[themes::kHeader]);
    OXO_EXPECT(!themes::FromStateText("").has_value());
    OXO_EXPECT(!themes::FromStateText("[]").has_value());
    OXO_EXPECT(!themes::FromStateText("{\"panel\":").has_value());
}

OXO_TEST(ThemeStateTextIsSwiftStateText) {
    // JSONEncoder(.sortedKeys) of the hex map, byte for byte.
    OXO_EXPECT_EQ(themes::StateText(themes::Synth2()),
                  std::string(R"({"button":"#363630","displayBottom":"#05171A","displayText":"#73F2FF",)"
                              R"("displayTop":"#123D40","header":"#3B3B38","headerText":"#E6E0B8",)"
                              R"("knobBody":"#171717","knobDial":"#FFFFFF","knobGradient":"#212121",)"
                              R"("label":"#1A1A0F","led":"#5CF266","panel":"#B7B79B","radio":"#FF331A"})"));
    for (const auto &f : themes::Factory()) OXO_EXPECT(themes::FromStateText(themes::StateText(f.palette)) == f.palette);
}

OXO_TEST(ThemeStoreReadsSwiftFile) {
    // Written by ThemeStore.persist (JSONEncoder: any key order, "/" escaped).
    const fs::path dir = FreshDir("oxo_theme_store_swift");
    std::ofstream(dir / "themes.json", std::ios::binary)
        << R"({"saved":[{"colors":{"panel":"#112233"},"name":"Night \/ Day \ud83c\udf19"},)"
           R"({"name":"Warm","colors":{"panel":"#313336","header":"#B78F0F","headerText":"#000000",)"
           R"("label":"#E4E7EC","knobBody":"#1B1B1B","knobGradient":"#181818","knobDial":"#B5B6B9",)"
           R"("button":"#363630","led":"#8ED9B5","radio":"#F3F9F1","displayText":"#C2D3E1",)"
           R"("displayTop":"#161A1A","displayBottom":"#0D100C"}}],"current":{"panel":"#445566"}})";
    themes::Store store(dir / "themes.json");
    OXO_EXPECT(store.Load());
    OXO_EXPECT_EQ(store.Current()[themes::kPanel], 0x445566u);
    OXO_EXPECT_EQ(store.Saved().size(), size_t(2));
    OXO_EXPECT_EQ(store.Saved()[0].name, std::string("Night / Day \xF0\x9F\x8C\x99"));
    OXO_EXPECT_EQ(store.Saved()[0].palette[themes::kPanel], 0x112233u);
    // A saved copy of a factory theme is not a user theme.
    OXO_EXPECT_EQ(store.UserThemes().size(), size_t(1));
    fs::remove_all(dir);
}

OXO_TEST(ThemeStoreRoundTrip) {
    const fs::path dir = FreshDir("oxo_theme_store_roundtrip");
    themes::Store a(dir / "sub" / "themes.json");
    OXO_EXPECT(!a.Load()); // no file yet: keeps the defaults
    OXO_EXPECT(a.Current() == themes::Synth2());
    themes::Palette p = themes::Synth2();
    p[themes::kPanel] = 0x102030;
    OXO_EXPECT(a.Remember(p));
    OXO_EXPECT(!a.Remember(p));
    OXO_EXPECT(a.Save(p, "  zeta "));
    OXO_EXPECT(a.Save(themes::Factory()[1].palette, "Alpha"));
    OXO_EXPECT(a.Save(p, "alpha2"));
    OXO_EXPECT(a.Save(themes::Factory()[2].palette, "zeta")); // same name = replaced
    OXO_EXPECT(!a.Save(p, "   "));
    themes::Store b(a.File());
    OXO_EXPECT(b.Load());
    OXO_EXPECT(b.Current() == p);
    OXO_EXPECT_EQ(b.Saved().size(), size_t(3));
    OXO_EXPECT_EQ(b.Saved()[0].name, std::string("Alpha"));
    OXO_EXPECT_EQ(b.Saved()[1].name, std::string("alpha2"));
    OXO_EXPECT_EQ(b.Saved()[2].name, std::string("zeta"));
    OXO_EXPECT(b.Saved()[2].palette == themes::Factory()[2].palette);
    OXO_EXPECT(b.Delete("Alpha"));
    themes::Store c(a.File());
    OXO_EXPECT(c.Load());
    OXO_EXPECT_EQ(c.Saved().size(), size_t(2));
    // A broken file keeps what is in memory.
    std::ofstream(a.File(), std::ios::binary | std::ios::trunc) << "{\"current\":";
    OXO_EXPECT(!c.Load());
    OXO_EXPECT_EQ(c.Saved().size(), size_t(2));
    fs::remove_all(dir);
}

OXO_TEST(JsonReaderIsStrict) {
    json::Value v;
    OXO_EXPECT(json::Parse("\xEF\xBB\xBF {\"a\": [1, -2.5e3, true, false, null, \"x\\n\\u00e9\"]} ", v));
    OXO_EXPECT(v.IsObject() && v.Find("a") && v.Find("a")->array.size() == 6);
    OXO_EXPECT_EQ(v.Find("a")->array[1].number, -2500.0);
    OXO_EXPECT_EQ(v.Find("a")->array[5].string, std::string("x\n\xC3\xA9"));
    OXO_EXPECT(!json::Parse("{\"a\":1} x", v));
    OXO_EXPECT(!json::Parse("{\"a\":01}", v));
    OXO_EXPECT(!json::Parse("{\"a\":\"\\x\"}", v));
    OXO_EXPECT(!json::Parse("{\"a\":\"\\ud83c\"}", v)); // lone surrogate
    OXO_EXPECT(!json::Parse("{\"a\":1,}", v));
    OXO_EXPECT(!json::Parse(std::string(100, '[') + std::string(100, ']'), v)); // too deep
    OXO_EXPECT_EQ(json::Quote("a\"b\\c\n"), std::string("\"a\\\"b\\\\c\\u000a\""));
}

// The Windows port's theme files (<data folder>\Themes\*.json, portable
// Synth2_Data\Themes): one per theme, name = file stem, its earlier 17-key
// format read leniently (the 4 voice-grid keys ignored).
OXO_TEST(ThemeFolderReadsWindowsFiles) {
    const fs::path dir = FreshDir("oxo_theme_folder");
    std::ofstream(dir / "Night.json", std::ios::binary)
        << "\xEF\xBB\xBF{\n  \"panel\": \"#102030\",\n  \"header\": \"#3B3B38\",\n"
           "  \"voiceGridBorder\": \"#040404\",\n  \"voiceGridPlaying\": \"#FFFFFF\"\n}\n";
    std::ofstream(dir / "Synth2.json", std::ios::binary) << themes::StateText(themes::Synth2()); // a seeded factory copy
    std::ofstream(dir / "Warm.json", std::ios::binary) << R"({"panel":"#000001"})";            // an edited one
    std::ofstream(dir / "broken.json", std::ios::binary) << "{\"panel\":";
    std::ofstream(dir / "list.json", std::ios::binary) << "[1, 2]";
    std::ofstream(dir / "notes.txt", std::ios::binary) << R"({"panel":"#FFFFFF"})";
    std::ofstream(dir / ".hidden.json", std::ios::binary) << R"({"panel":"#FFFFFF"})";
    std::ofstream(dir / "Caf\xC3\xA9.JSON", std::ios::binary) << R"({"label":"#ABCDEF"})";
    const auto all = themes::LoadFolder(dir);
    OXO_EXPECT_EQ(all.size(), size_t(4)); // Café, Night, Synth2, Warm: sorted, case-insensitive
    const auto shown = themes::WithoutFactoryCopies(all);
    OXO_EXPECT_EQ(shown.size(), size_t(3));
    if (shown.size() == 3) {
        OXO_EXPECT_EQ(shown[0].name, std::string("Caf\xC3\xA9"));
        OXO_EXPECT_EQ(shown[0].palette[themes::kLabel], 0xABCDEFu);
        OXO_EXPECT_EQ(shown[1].name, std::string("Night"));
        OXO_EXPECT_EQ(shown[1].palette[themes::kPanel], 0x102030u);
        OXO_EXPECT_EQ(shown[1].palette[themes::kLed], themes::Synth2()[themes::kLed]);
        OXO_EXPECT_EQ(shown[2].name, std::string("Warm"));
    }
    OXO_EXPECT(themes::LoadFolder(dir / "missing").empty());
    fs::remove_all(dir);
}
