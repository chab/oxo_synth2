#pragma once

// Windows-only filesystem locations. Keep the macOS implementation in
// EditorCore/Storage.hpp unchanged. Module lookup is based on the DLL itself
// so this works regardless of the DAW executable or its current directory.
#ifndef _WIN32
#error WindowsStorage.hpp is for Windows builds only
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>

#include "EditorCore/Identity.h"
#include "Windows_Build/Source/StorageSelection.hpp"
#include "Windows_Build/Source/WindowsModule.hpp"

namespace oxo::storage::win {
namespace fs = std::filesystem;

inline fs::path ThisModule() {
    // This DLL, rather than the hosting DAW's EXE.
    const HMODULE module = oxo::win::ModuleInstance();
    if (!module) return {};
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(module, buffer.data(), DWORD(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    buffer.resize(count);
    return fs::path(buffer);
}

inline fs::path Bundle() {
    const fs::path module = ThisModule();
    if (module.empty()) return {};
    // First find an OUTER *.vst3 bundle folder. The DLL INSIDE that bundle
    // can itself be called OxoSynth2.vst3; its extension is NOT the bundle.
    fs::path folder = module.parent_path();
    for (int i = 0; i < 8 && !folder.empty(); ++i) {
        if (_wcsicmp(folder.extension().c_str(), L".vst3") == 0) return folder;
        const fs::path next = folder.parent_path();
        if (next == folder) break;
        folder = next;
    }
    // Older VST3 installations may use a single *.vst3 binary, no bundle.
    if (_wcsicmp(module.extension().c_str(), L".vst3") == 0) return module;
    return {};
}

inline fs::path KnownFolder(REFKNOWNFOLDERID folder) {
    PWSTR raw = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        p = fs::path(raw);
    }
    if (raw) CoTaskMemFree(raw);
    return p;
}

inline fs::path StandardData() {
    const fs::path base = KnownFolder(FOLDERID_RoamingAppData);
    return base.empty() ? fs::path{} : base / fs::path(OXO_DATA_FOLDER);
}
// The bank library, as on the mac (~/Music/<OXO_LIBRARY_FOLDER>): a
// documents folder, not the roaming settings one (roaming profiles sync
// it at every logon; big libraries made that slow).
inline fs::path StandardLibrary() {
    const fs::path base = KnownFolder(FOLDERID_Documents);
    return base.empty() ? fs::path{} : base / fs::path(OXO_LIBRARY_FOLDER);
}

inline paths::Selection Chosen() {
    const fs::path bundle = Bundle();
    const fs::path parent = bundle.empty() ? fs::path{} : bundle.parent_path();
    return paths::Choose(parent, StandardData(), StandardLibrary(), [](const fs::path &p) {
        std::error_code ec;
        return fs::is_directory(p, ec);
    });
}

inline fs::path Factory() {
    const fs::path bundle = Bundle();
    return bundle.empty() ? fs::path{} : bundle / "Contents" / "Resources" / s2::kFactoryBankName;
}
} // namespace oxo::storage::win
