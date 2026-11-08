#pragma once

// Editor services shared by every wrapper (mac VST3 controller, iPad AUv3
// instance): patch name / serial, patch files, the bank library and the C
// hooks handed to the SwiftUI editor. A wrapper subclasses it and provides
// parameter access + MIDI learn storage. UI thread only.

#include "EngineCore/InitPatch.hpp"
#include "EngineCore/MidiLearn.hpp"
#include "EngineCore/ParamFormat.hpp"
#include "EngineCore/PatchLibrary.hpp"
#include "EditorCore/OxoSynth2Bridge.h"
#include "EditorCore/Trash.hpp"

#if defined(_WIN32)
#include "Windows_Build/Source/WindowsPresetIndex.hpp"
#endif

#include <algorithm>
#include <fstream>
#include <string>

namespace oxo {

class SynthEditorCore {
public:
    virtual ~SynthEditorCore() = default;

    // ---- provided by the wrapper ----
    virtual int32_t NativeValue(int id) = 0;
    virtual void BeginEdit(int id) = 0;
    virtual void PerformEdit(int id, int32_t value) = 0; // host-visible, inside Begin/End
    virtual void EndEdit(int id) = 0;
    virtual void PatchLoading() {}                        // before a load's edits: the DSP empties its delay, fades the switch
    virtual void GroupEdit(bool /*begin*/) {}             // a load's 99 edits = one host undo step where supported
    virtual void PatchNameChanged(const std::string &) {} // e.g. tell the processor
    virtual void ProgramChanged(const s2::LibraryProgram &) {} // ditto (the state chunk keeps it)
    virtual void ThemeChanged(const std::string &) {}           // ditto
    virtual int32_t MidiCC(int id) = 0;
    virtual void SetMidiCC(int id, int32_t cc) = 0;
    virtual int32_t LastCC() = 0;
    virtual uint64_t VoiceStates() = 0; // s2::Synth::VoiceStates as last seen by the UI side
    virtual void AllNotesOff() = 0;     // menu: s2::Synth::Panic on the audio side

    // ---- shared ----
    // `factory` = the read-only factory bank in the plugin bundle, `build` =
    // the plugin's CFBundleVersion (starter banks: once per build).
    void ConfigureLibrary(const std::filesystem::path &root, const std::filesystem::path &factory = {},
                          const std::string &build = {}) {
        library_.Configure(root, factory, build);
        library_.SetTrash(MoveToTrash);
#if defined(_WIN32)
        // Restore the bank index immediately; the browser's Refresh command
        // still performs a full authoritative filesystem scan on demand.
        std::vector<s2::PatchBank> cached;
        if (winindex::Load(root.parent_path(),root,factory,cached)) {
            library_.AdoptIndex(std::move(cached));
            savedIndexRevision_ = library_.Revision();
        }
#endif
    }
    s2::PatchManager &Library() { return library_; }

    const std::string &PatchName() const { return patchName_; }
    int32_t PatchSerial() const { return serial_; }

    void SetPatchName(const std::string &name) {
        patchName_ = name;
        PatchNameChanged(name);
    }

    // Host / session restore: the wrapper already holds the values.
    void RestoredPatch(const std::string &name, const std::string &color, const s2::LibraryProgram &program = {}) {
        patchName_ = name;
        patchColor_ = color;
        program_ = program;
        ++serial_;
    }

    // The library program the sound came from (LCD "NNN:name" + bank line,
    // browser selection), none after Init / a patch file.
    const s2::LibraryProgram &Program() const { return program_; }

    // This instance's panel palette (opaque text owned by the editor, ""
    // = never set): session state, untouched by patch loads. Restore it
    // BEFORE RestoredPatch, whose serial bump makes an open editor re-read it.
    const std::string &Theme() const { return theme_; }
    void RestoredTheme(const std::string &theme) { theme_ = theme; }

    // A fresh instance (no session state): the first patch of the library
    // (Synth1 starts on program 001 of its first bank). False = empty library.
    bool StartupProgram(s2::Sy1Patch &patch, s2::LibraryProgram &where) const {
        return s2::FirstProgram(library_.Root(), library_.Factory(), patch, where);
    }

    s2::Sy1Patch CurrentPatch() {
        s2::Sy1Patch p;
        p.name = patchName_;
        p.color = patchColor_;
        for (int i = 0; i < s2::kParamCount; ++i) p.values[i] = NativeValue(i);
        return p;
    }

    // A patch load = one host-visible edit per parameter (reaches the DSP,
    // undoable in hosts that record parameter edits).
    void ApplyPatch(const s2::Sy1Patch &patch) {
        PatchLoading();
        GroupEdit(true);
        for (int i = 0; i < s2::kParamCount; ++i) {
            BeginEdit(i);
            PerformEdit(i, s2::ClampValue(i, patch.values[i]));
            EndEdit(i);
        }
        GroupEdit(false);
        patchColor_ = patch.color;
        SetPatchName(patch.name);
        SetProgram({});
        ++serial_;
    }

    bool LoadPatchFile(const std::string &path) {
        std::string text;
        if (!s2::ReadTextFile(path, text)) return false;
        s2::Sy1Patch patch = s2::ParseSy1(text);
        patch.name = s2::LegacyToUtf8(patch.name);
        ApplyPatch(patch);
        return true;
    }
    bool SavePatchFile(const std::string &path) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << s2::WriteSy1(CurrentPatch());
        return bool(f);
    }
    bool LoadLibraryPatch(int bank, int slot) {
        s2::Sy1Patch p;
        if (!library_.Load(bank, slot, p)) return false;
        ApplyPatch(p);
        SetProgram({ProgramBank(size_t(bank)), slot});
        return true;
    }
    // Browser drag and drop (move / swap within a bank); the loaded program
    // follows its patch.
    bool MovePatch(int bank, int from, int to) {
        library_.EnsureScanned();
        const auto &banks = library_.Library().Banks();
        if (bank < 0 || size_t(bank) >= banks.size()) return false;
        const std::string rel = ProgramBank(size_t(bank));
        if (!library_.Move(bank, from, to)) return false;
        if (program_.slot >= 0 && program_.bank == rel && (program_.slot == from || program_.slot == to))
            SetProgram({rel, program_.slot == from ? to : from});
        return true;
    }

    // Browser > Delete bank (PatchManager::DeleteBank); the sound playing
    // stays, but no longer points into the deleted bank.
    bool DeleteBank(int bank) {
        library_.EnsureScanned();
        if (bank < 0 || size_t(bank) >= library_.Library().Banks().size()) return false;
        const std::string rel = ProgramBank(size_t(bank));
        const bool ok = library_.DeleteBank(bank);
        if (program_.slot >= 0 && program_.bank == rel) SetProgram({});
        return ok;
    }

    // Synth1 "write": the current sound, renamed, into a bank slot.
    bool WriteLibraryPatch(int bank, int slot, const std::string &name) {
        s2::Sy1Patch p = CurrentPatch();
        if (!name.empty()) p.name = name;
        if (!library_.Write(bank, slot, p)) return false;
        if (p.name != patchName_) SetPatchName(p.name);
        SetProgram({ProgramBank(size_t(bank)), slot});
        ++serial_;
        return true;
    }

    OxoSynth2Hooks MakeHooks() {
        OxoSynth2Hooks h{};
        h.ctx = this;
        h.getValue = [](void *c, int32_t id) { return id >= 0 && id <= s2::kVolumeParamId ? C(c)->NativeValue(id) : 0; };
        h.beginEdit = [](void *c, int32_t id) { C(c)->BeginEdit(id); };
        h.setValue = [](void *c, int32_t id, int32_t v) {
            if (id >= 0 && id <= s2::kVolumeParamId) C(c)->PerformEdit(id, s2::ClampValue(id, v));
        };
        h.endEdit = [](void *c, int32_t id) { C(c)->EndEdit(id); };
        h.paramInfo = [](void *, int32_t id, int32_t *mn, int32_t *mx, int32_t *df) {
            const s2::ParamInfo &p = s2::Info(id);
            *mn = p.min, *mx = p.max; // def = the double-click target
            *df = id == s2::kVolumeParamId ? p.def : s2::kInitPatchValues[size_t(std::clamp<int>(id, 0, s2::kParamCount - 1))];
        };
        h.paramName = [](void *, int32_t id) { return s2::Info(id).name; };
        h.displayName = [](void *c, int32_t id) { return s2::DisplayName(id, C(c)->NativeValue(s2::kEffectType)); };
        h.formatValue = [](void *c, int32_t id, int32_t v) {
            const int sync = s2::TempoSyncSwitch(id);
            return C(c)->Keep(s2::FormatParam(id, v, sync >= 0 && C(c)->NativeValue(sync),
                                              C(c)->NativeValue(s2::kEffectType)));
        };
        h.patchName = [](void *c) { return C(c)->patchName_.c_str(); };
        h.setPatchName = [](void *c, const char *n) { C(c)->SetPatchName(n ? n : ""); };
        h.patchSerial = [](void *c) { return C(c)->serial_; };
        h.patchText = [](void *c) { return C(c)->Keep(s2::WriteSy1(C(c)->CurrentPatch())); };
        h.loadPatchFile = [](void *c, const char *p) { return int32_t(p && C(c)->LoadPatchFile(p)); };
        h.savePatchFile = [](void *c, const char *p) { return int32_t(p && C(c)->SavePatchFile(p)); };
        h.libraryJSON = [](void *c) {
            C(c)->json_ = C(c)->library_.Json();
            return C(c)->json_.c_str();
        };
        h.rescanLibrary = [](void *c) { C(c)->library_.Rescan(); };
        h.startLibraryScan = [](void *c) { C(c)->library_.StartRescan(); };
        h.libraryRevision = [](void *c) {
            auto* editor = C(c);
            editor->library_.PollRescan();
#if defined(_WIN32)
            const int revision = editor->library_.Revision();
            if (revision > 0 && revision != editor->savedIndexRevision_) {
                // Writes, imports and explicit refreshes update the index.
                // Even when cache writing fails, don't retry every 50ms.
                editor->savedIndexRevision_ = revision;
                winindex::Save(editor->library_.Root().parent_path(),
                               editor->library_.Root(),editor->library_.Factory(),
                               editor->library_.Library().Banks());
            }
#endif
            return int32_t(editor->library_.Revision());
        };
        h.libraryScanning = [](void *c) { return int32_t(C(c)->library_.Scanning()); };
        h.libraryNotice = [](void *c) { return C(c)->Keep(C(c)->library_.TakeNotice()); };
        h.libraryRoot = [](void *c) { return C(c)->Keep(C(c)->library_.Root().string()); };
        h.loadLibraryPatch = [](void *c, int32_t b, int32_t s) { return int32_t(C(c)->LoadLibraryPatch(b, s)); };
        h.writeLibraryPatch = [](void *c, int32_t b, int32_t s, const char *n) {
            return int32_t(C(c)->WriteLibraryPatch(b, s, n ? n : ""));
        };
        h.slotPath = [](void *c, int32_t b, int32_t s) { return C(c)->Keep(C(c)->library_.SlotFile(b, s)); };
        h.newBank = [](void *c, const char *n) { return int32_t(C(c)->library_.NewBank(n ? n : "")); };
        h.movePatch = [](void *c, int32_t b, int32_t from, int32_t to) { return int32_t(C(c)->MovePatch(b, from, to)); };
        h.copyPatch = [](void *c, int32_t b, int32_t s, int32_t to) { return int32_t(C(c)->library_.Copy(b, s, to)); };
        h.deleteBank = [](void *c, int32_t b) { return int32_t(C(c)->DeleteBank(b)); };
        h.exportBank = [](void *c, int32_t b, const char *d) {
            return C(c)->Keep(d ? C(c)->library_.ExportBank(b, d) : std::string());
        };
        h.importItems = [](void *c, const char *p) { return C(c)->Keep(C(c)->library_.Import(p ? p : "")); };
        h.midiCC = [](void *c, int32_t id) { return C(c)->MidiCC(id); };
        h.setMidiCC = [](void *c, int32_t id, int32_t cc) { C(c)->SetMidiCC(id, cc); };
        h.lastCC = [](void *c) { return C(c)->LastCC(); };
        h.voiceStates = [](void *c) { return C(c)->VoiceStates(); };
        h.allNotesOff = [](void *c) { C(c)->AllNotesOff(); };
        h.loadedProgram = [](void *c, int32_t *bank, int32_t *slot) {
            const s2::LibraryProgram &p = C(c)->program_;
            *bank = *slot = -1;
            if (p.slot < 0) return;
            C(c)->library_.EnsureScanned();
            *bank = s2::FindProgramBank(C(c)->library_.Library(), C(c)->library_.Root(), p.bank);
            if (*bank >= 0) *slot = p.slot;
        };
        h.initializePatch = [](void *c) { C(c)->ApplyPatch(s2::InitPatch()); };
        h.themeText = [](void *c) { return C(c)->theme_.c_str(); };
        h.setThemeText = [](void *c, const char *t) {
            const std::string theme = t ? t : "";
            if (theme == C(c)->theme_) return;
            C(c)->theme_ = theme;
            C(c)->ThemeChanged(theme);
        };
        return h;
    }

protected:
    std::string ProgramBank(size_t bank) const { return s2::ProgramBank(library_.Library(), library_.Root(), bank); }

#if defined(_WIN32)
    int savedIndexRevision_ = 0;
#endif
    std::string patchName_ = "Init";
    std::string patchColor_ = "default";
    int32_t serial_ = 0;
    s2::PatchManager library_;
    s2::LibraryProgram program_;
    std::string theme_;

private:
    void SetProgram(const s2::LibraryProgram &p) {
        if (p == program_) return;
        program_ = p;
        ProgramChanged(p);
    }
    static SynthEditorCore *C(void *ctx) { return static_cast<SynthEditorCore *>(ctx); }
    const char *Keep(std::string s) {
        scratch_ = std::move(s);
        return scratch_.c_str();
    }
    std::string scratch_, json_;
};

} // namespace oxo
