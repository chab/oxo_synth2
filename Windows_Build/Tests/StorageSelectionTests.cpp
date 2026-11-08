#include "Windows_Build/Source/StorageSelection.hpp"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace paths = oxo::storage::paths;

int main() {
    const fs::path staging = fs::temp_directory_path() /
        ("oxo_synth2_storage_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path bundleDirectory = staging / "Plugins";
    const fs::path local = bundleDirectory / "Synth2_Data";
    const fs::path standardData = staging / "Roaming" / "Oxo Music" / "Oxo Synth2";
    const fs::path standardLibrary = staging / "Music" / "Oxo Music" / "Oxo Synth2";
    std::error_code ec;
    fs::create_directories(bundleDirectory, ec);
    if (ec) return 1;
    auto select = [&] { return paths::Choose(bundleDirectory, standardData, standardLibrary,
                                            [](const fs::path &p) { std::error_code error;
                                                return fs::is_directory(p, error); }); };
    auto fail = [&](const char *message) { std::cerr << "FAIL " << message << '\n'; fs::remove_all(staging, ec); return 1; };
    auto normal = select();
    if (normal.portable || normal.data != standardData || normal.library != standardLibrary)
        return fail("normal mode must not be changed");
    std::cout << "PASS normal folders without portable data\n";

    fs::create_directories(local, ec);
    if (ec) return fail("create temporary local data folder");
    auto portable = select();
    if (!portable.portable || portable.data != local || portable.library != local / "Presets")
        return fail("portable location detection");
    if (portable.standardData != standardData || portable.standardLibrary != standardLibrary)
        return fail("standard paths must remain accessible");
    std::cout << "PASS portable selection and original paths retained\n";

    fs::remove_all(local, ec);
    auto restored = select();
    if (restored.portable || restored.data != standardData || restored.library != standardLibrary)
        return fail("normal mode after removing portable folder");
    std::cout << "PASS return to normal mode when folder removed\n";
    fs::remove_all(staging, ec);
    std::cout << "3 tests, 0 failed\n";
    return 0;
}
