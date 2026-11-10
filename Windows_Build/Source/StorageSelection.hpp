#pragma once

// Platform-neutral choice of data roots. Nothing here moves or edits a preset.
// The two original user-folder paths remain available in portable mode.
#include <filesystem>

namespace oxo::storage::paths {
namespace fs = std::filesystem;

struct Selection {
    fs::path data;
    fs::path library;
    fs::path standardData;
    fs::path standardLibrary;
    bool portable = false;
};

// pluginDirectory = the folder containing the *.vst3 bundle, not its Contents.
// Supplying an existence predicate makes this choice testable without Windows.
template <typename IsDirectory>
Selection Choose(const fs::path &pluginDirectory, const fs::path &standardData,
                 const fs::path &standardLibrary, IsDirectory isDirectory) {
    Selection out{standardData, standardLibrary, standardData, standardLibrary, false};
    if (!pluginDirectory.empty()) {
        const fs::path local = pluginDirectory / "Synth2_Data";
        if (isDirectory(local)) {
            out.data = local;
            out.library = local / "Presets";
            out.portable = true;
        }
    }
    return out;
}
} // namespace oxo::storage::paths
