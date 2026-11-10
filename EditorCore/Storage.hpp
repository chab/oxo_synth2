#pragma once

// Per-user files under ~/Library/Application Support/<OXO_DATA_FOLDER>
// (on iPad: inside the extension's own container, $HOME is sandboxed).

#include "EditorCore/Identity.h"
#include "EditorCore/Settings.hpp"
#include "EngineCore/MidiLearn.hpp"
#include "EngineCore/PatchLibrary.hpp"

#include <cstdlib>
#if defined(_WIN32)
#include "Windows_Build/Source/WindowsStorage.hpp"
#else
#include <dlfcn.h>
#endif
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace oxo::storage {

inline std::filesystem::path Root() {
#if defined(_WIN32)
    return win::Chosen().data;
#else
    const char *home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "") / "Library/Application Support" / OXO_DATA_FOLDER;
#endif
}

#ifdef OXO_LIBRARY_FOLDER
// mac bank library: ~/Music/<OXO_LIBRARY_FOLDER> (Oxo Drums' "~/Music/Oxo
// Music/<product>" convention), the only folder read; banks get in by Import.
inline std::filesystem::path LibraryRoot() {
#if defined(_WIN32)
    return win::Chosen().library;
#else
    const char *home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "") / "Music" / OXO_LIBRARY_FOLDER;
#endif
}

// The factory bank: <this bundle>/Contents/Resources/Synth2 Factory, found
// from our own binary (the VST3, or the copy nested in the AUv2 component).
inline std::filesystem::path FactoryRoot() {
#if defined(_WIN32)
    return win::Factory();
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void *>(&FactoryRoot), &info) || !info.dli_fname) return {};
    return std::filesystem::path(info.dli_fname).parent_path().parent_path() / "Resources" / s2::kFactoryBankName;
#endif
}
#endif

inline std::filesystem::path MidiLearnPath() { return Root() / "midi_mappings.json"; }

inline s2::MidiLearnTable LoadMidiLearn() {
    std::ifstream f(MidiLearnPath(), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return s2::DecodeMidiLearn(ss.str());
}

inline bool SaveMidiLearn(const s2::MidiLearnTable &cc) {
    std::error_code ec;
    std::filesystem::create_directories(Root(), ec);
    const std::filesystem::path path = MidiLearnPath(), tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << s2::EncodeMidiLearn(cc);
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

// Menu > Program change: MIDI program changes played or ignored (default).
// Per user (settings.json), read by each instance when it starts.
inline constexpr const char *kProgramChangeKey = "programChange";
inline bool LoadProgramChange() { return settings::LoadBool(Root(), kProgramChangeKey, false); }
inline bool SaveProgramChange(bool on) { return settings::SaveBool(Root(), kProgramChangeKey, on); }

} // namespace oxo::storage
