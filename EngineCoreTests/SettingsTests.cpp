#include "TestFramework.hpp"

#include "../EditorCore/Settings.hpp"
#include "../Windows_Build/Source/WindowsZoomSettings.hpp"

namespace fs = std::filesystem;
using namespace oxo;

namespace {
fs::path FreshDir(const char *name) {
    const fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}
std::string Text(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void WriteText(const fs::path &p, const std::string &s) { std::ofstream(p, std::ios::binary) << s; }
} // namespace

// settings.json: independent top-level keys, each save edits only its own
// value; nested / quoted look-alikes and unknown value types are left alone.
OXO_TEST(SettingsKeepOtherKeys) {
    const fs::path root = FreshDir("oxo_s2_settings_test");
    const fs::path file = root / settings::kFileName;
    OXO_EXPECT(settings::LoadBool(root, "programChange", true));
    OXO_EXPECT(settings::SaveBool(root, "programChange", false));
    OXO_EXPECT(Text(file) == "{\n  \"programChange\": false\n}\n");
    OXO_EXPECT(!settings::LoadBool(root, "programChange", true));

    OXO_EXPECT(winzoom::SaveWidth(root, 900, 525, 1400));
    OXO_EXPECT_EQ(winzoom::LoadWidth(root, 700, 525, 1400), 900);
    OXO_EXPECT(settings::SaveBool(root, "programChange", true));
    OXO_EXPECT(Text(file) == "{\n  \"programChange\": true,\n  \"windowsEditorWidth\": 900\n}\n");

    WriteText(file, "{\"nested\": {\"programChange\": false}, \"s\": \"programChange\", \"programChange\": false, \"x\": [1]}");
    OXO_EXPECT(!settings::LoadBool(root, "programChange", true));
    OXO_EXPECT(settings::SaveBool(root, "programChange", true));
    OXO_EXPECT(Text(file) == "{\"nested\": {\"programChange\": false}, \"s\": \"programChange\", \"programChange\": true, \"x\": [1]}");

    WriteText(file, "{\"programChange\": \"yes\"}");
    OXO_EXPECT(settings::LoadBool(root, "programChange", true));
    OXO_EXPECT(!settings::SaveBool(root, "programChange", false)); // unknown type: kept
    OXO_EXPECT(Text(file) == "{\"programChange\": \"yes\"}");
    WriteText(file, "not json");
    OXO_EXPECT(!settings::SaveBool(root, "programChange", false));
    OXO_EXPECT(Text(file) == "not json");
    WriteText(file, " { } ");
    OXO_EXPECT(settings::SaveBool(root, "programChange", false));
    OXO_EXPECT(Text(file) == " { \n  \"programChange\": false\n} ");
    WriteText(file, "{\"windowsEditorWidth\": -3}");
    OXO_EXPECT_EQ(winzoom::LoadWidth(root, 700, 525, 1400), 700);
}
