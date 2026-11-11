#pragma once

// C bridge between the plugin wrapper (C++: the VST3 controller on mac, the
// AUv3 instance on iPad, both `SynthEditorCore`) and the SwiftUI editor
// (Shared/, `HooksSynthHost`). Everything runs on the UI thread. Strings
// returned by the hooks stay valid until the next call of the same hook.

#include <stdint.h>

#include "Identity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OxoSynth2Hooks {
    void *ctx;
    int32_t (*getValue)(void *ctx, int32_t id);                // native value
    void (*beginEdit)(void *ctx, int32_t id);
    void (*setValue)(void *ctx, int32_t id, int32_t value);    // host-visible edit
    void (*endEdit)(void *ctx, int32_t id);
    void (*paramInfo)(void *ctx, int32_t id, int32_t *min, int32_t *max, int32_t *def); // def = INIT's value
    const char *(*paramName)(void *ctx, int32_t id);
    const char *(*displayName)(void *ctx, int32_t id);         // readout name (effect knobs per type)
    const char *(*formatValue)(void *ctx, int32_t id, int32_t value);
    const char *(*patchName)(void *ctx);
    void (*setPatchName)(void *ctx, const char *name);
    int32_t (*patchSerial)(void *ctx);                         // bumps on every load / restore
    const char *(*patchText)(void *ctx);                       // current patch as .sy1 text
    int32_t (*loadPatchFile)(void *ctx, const char *path);
    int32_t (*savePatchFile)(void *ctx, const char *path);
    // Library (EngineCore/PatchLibrary.hpp): one folder, banks of 128 slots.
    const char *(*libraryJSON)(void *ctx);                     // PatchManager::Json
    void (*rescanLibrary)(void *ctx); // also extracts + trashes zips put into the library
    // The same rescan on a worker thread (browser open, Refresh); the UI
    // keeps polling libraryRevision, which adopts the finished scan.
    void (*startLibraryScan)(void *ctx);
    int32_t (*libraryRevision)(void *ctx); // bumps whenever the list changes, 0 = never scanned
    int32_t (*libraryScanning)(void *ctx); // a background scan is running
    const char *(*libraryNotice)(void *ctx); // "X.zip imported and moved to Trash", "" = none; clears
    const char *(*libraryRoot)(void *ctx);
    int32_t (*loadLibraryPatch)(void *ctx, int32_t bank, int32_t slot);
    int32_t (*writeLibraryPatch)(void *ctx, int32_t bank, int32_t slot, const char *name); // Synth1 "write"
    const char *(*slotPath)(void *ctx, int32_t bank, int32_t slot); // .sy1 file, "" = empty
    int32_t (*newBank)(void *ctx, const char *name);           // -> bank index, -1 = failed
    int32_t (*movePatch)(void *ctx, int32_t bank, int32_t from, int32_t to); // drag and drop: move / swap (renames files)
    // Drag onto a bank: copy into its first free slot -> the slot, -1 = full, -2 = failed.
    int32_t (*copyPatch)(void *ctx, int32_t bank, int32_t slot, int32_t toBank);
    int32_t (*deleteBank)(void *ctx, int32_t bank); // folder to the Trash (mac) / deleted (iPad); not the factory bank
    // Copies a bank as a new folder into `dir` -> that folder, "" = failed.
    const char *(*exportBank)(void *ctx, int32_t bank, const char *dir);
    // Copies folders / extracts zips / loose .sy1 into the library and
    // rescans; paths newline-separated in, new top-level folders out.
    const char *(*importItems)(void *ctx, const char *paths);
    // MIDI learn (plugin-global table, persisted by the wrapper).
    int32_t (*midiCC)(void *ctx, int32_t id);                  // 0 = not learned
    void (*setMidiCC)(void *ctx, int32_t id, int32_t cc);      // 0 = unlearn
    int32_t (*lastCC)(void *ctx);                              // (counter << 7 | cc), 0 = none yet
    void (*initializePatch)(void *ctx);                        // load Synth2's INIT (EngineCore/InitPatch.hpp)
    uint64_t (*voiceStates)(void *ctx); // 2 bits per voice (2i): 0 idle, 1 releasing, 2 held
    // Library program the sound came from (index into libraryJSON's banks), -1 / -1 = none.
    void (*loadedProgram)(void *ctx, int32_t *bank, int32_t *slot);
    void (*allNotesOff)(void *ctx); // menu: cut every voice now, forget keys / pedal / arp
    // This instance's panel palette (session state, opaque JSON written by
    // the editor), "" = never set. Re-read when patchSerial bumps.
    const char *(*themeText)(void *ctx);
    void (*setThemeText)(void *ctx, const char *text);
    // Menu > Program change: MIDI program changes played (1) or ignored (0);
    // a per-user preference, saved by the wrapper.
    int32_t (*programChange)(void *ctx);
    void (*setProgramChange)(void *ctx, int32_t on);
} OxoSynth2Hooks;

// Asks the host for a new editor width (the height follows the aspect):
// the "Synth2" logo's UI size menu. Main thread.
typedef void (*OxoSynth2ResizeRequest)(void *ctx, double width);

// Implemented in Swift (@_cdecl), mac VST3 only.
void *OxoSynth2EditorCreate(const OxoSynth2Hooks *hooks, double width, double height, void *resizeCtx,
                            OxoSynth2ResizeRequest requestResize);
void OxoSynth2EditorRelease(void *nsView);
double OxoSynth2EditorAspect(void); // design height / width

#ifdef __cplusplus
}
#endif
