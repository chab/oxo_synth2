#include "TestFramework.hpp"

#include "../EditorCore/UpdateCheck.hpp"

#include <fstream>
#include <sstream>

using namespace oxo;

namespace {
const char *kJson = R"({"products":{"oxo_synth2":{"mac":{"version":"1.1.2","build":6,"url":"u"},)"
                    R"("win":{"version":"1.2","build":9,"url":"w"},"page":"https://oxo-music.com/products/oxo_synth2"},)"
                    R"("oxo_steps":{"mac":{"version":"1.17","build":19}}}})";
const std::string kSite = "https://oxo-music.com";

update::State Win(const std::string &version, int build, const char *json = kJson) {
    return update::Evaluate(json, "oxo_synth2", "win", version, build, kSite);
}
} // namespace

OXO_TEST(UpdateNewerBuildIsAvailable) {
    const auto s = Win("1.1.2", 6);
    OXO_EXPECT(s.kind == update::Kind::Available);
    OXO_EXPECT_EQ(s.latest, std::string("V1.2"));
    OXO_EXPECT_EQ(s.installed, std::string("V1.1.2"));
    OXO_EXPECT_EQ(s.page, std::string("https://oxo-music.com/products/oxo_synth2"));
}

OXO_TEST(UpdateSameVersionShowsBuilds) {
    const auto s = Win("1.2", 8);
    OXO_EXPECT(s.kind == update::Kind::Available);
    OXO_EXPECT_EQ(s.latest, std::string("V1.2 (build 9)"));
    OXO_EXPECT_EQ(s.installed, std::string("V1.2 (build 8)"));
}

// Only this platform's entry counts; same / higher build, no entry for the
// platform or the product = up to date.
OXO_TEST(UpdateLatestCases) {
    OXO_EXPECT(Win("1.2", 9).kind == update::Kind::Latest);
    OXO_EXPECT(Win("1.3", 10).kind == update::Kind::Latest);
    OXO_EXPECT_EQ(Win("1.2", 9).installed, std::string("V1.2"));
    OXO_EXPECT(update::Evaluate(kJson, "oxo_steps", "win", "1.0", 1, kSite).kind == update::Kind::Latest);
    OXO_EXPECT(update::Evaluate(kJson, "nothing", "mac", "1.0", 1, kSite).kind == update::Kind::Latest);
    // the mac entry is newer than this build: not Windows' business
    OXO_EXPECT(update::Evaluate(kJson, "oxo_synth2", "mac", "1.1.2", 6, kSite).kind == update::Kind::Latest);
}

OXO_TEST(UpdateNotOurJsonIsUnavailable) {
    OXO_EXPECT(Win("1.0", 1, "<html>502</html>").kind == update::Kind::Unavailable);
    OXO_EXPECT(Win("1.0", 1, R"({"other":1})").kind == update::Kind::Unavailable);
}

OXO_TEST(UpdatePageFallback) {
    const auto s = Win("1.0", 1, R"({"products":{"oxo_synth2":{"win":{"version":"1.1","build":2}}}})");
    OXO_EXPECT_EQ(s.page, std::string("https://oxo-music.com/products/oxo_synth2"));
}

OXO_TEST(UpdateHelpers) {
    OXO_EXPECT_EQ(update::SiteName("https://oxo-music.com"), std::string("oxo-music.com"));
    OXO_EXPECT_EQ(update::SiteName("https://a.b/c"), std::string("a.b"));
    OXO_EXPECT_EQ(update::Email("mailto:contact@oxo-music.com"), std::string("contact@oxo-music.com"));
}

// The texts are Shared/SynthUpdateCheck.swift's: change both or neither.
OXO_TEST(UpdateMessagesMatchSwift) {
    std::ifstream f(std::string(OXO_TESTS_DIR) + "/../Shared/SynthUpdateCheck.swift", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string swift = ss.str();
    const update::State latest{update::Kind::Latest, {}, "VX", {}};
    const update::State available{update::Kind::Available, "VL", "VI", "PAGE"};
    const std::pair<std::string, std::string> cases[] = {
        {update::Message({update::Kind::Checking, {}, {}, {}}, "SITE", "MAIL"), R"(return "Checking \(siteName)…")"},
        {update::Message(latest, "SITE", "MAIL"), R"(return "You're using the latest version (\(installed)).")"},
        {update::Message(available, "SITE", "MAIL"),
         R"(return "A new version \(latest) is available on \(page.absoluteString)\n\nYour version is \(installed).")"},
        {update::Message({}, "SITE", "MAIL"),
         R"(return "\(siteName) is not available. Please try again later, or contact \(email) if the problem persists.")"},
    };
    for (const auto &[cpp, swiftLine] : cases) {
        OXO_EXPECT(swift.find(swiftLine) != std::string::npos);
        // the C++ text = the Swift line with its placeholders filled
        std::string expected = swiftLine.substr(8, swiftLine.size() - 9); // inside return "…"
        for (const auto &[from, to] : {std::pair<std::string, std::string>{"\\(siteName)", "SITE"},
                                       {"\\(installed)", cpp.find("VI") != std::string::npos ? "VI" : "VX"},
                                       {"\\(latest)", "VL"}, {"\\(page.absoluteString)", "PAGE"},
                                       {"\\(email)", "MAIL"}, {"\\n", "\n"}})
            for (size_t p; (p = expected.find(from)) != std::string::npos;) expected.replace(p, from.size(), to);
        OXO_EXPECT_EQ(cpp, expected);
    }
}
