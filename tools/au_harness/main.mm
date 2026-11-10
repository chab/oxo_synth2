// Mac-side check of the iPad AUv3 C bridge (render, MIDI 1/2, learn, state,
// library hooks) without a device:
//   clang++ -std=c++20 -fobjc-arc -I. OxoSynth2AudioUnit/DSP/Synth2Bridge.mm tools/au_harness/main.mm \
//     -framework AudioToolbox -framework CoreMIDI -lz -o build/au_harness && HOME=/tmp/s2h ./build/au_harness
#include "OxoSynth2AudioUnit/DSP/Synth2Bridge.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
int main() {
    int fails = 0;
    auto expect = [&](bool ok, const char *what) { if (!ok) { std::printf("FAIL %s\n", what); ++fails; } };
    std::system("rm -rf /tmp/oxo_s2_ios_harness_lib"); // the checks expect a fresh library ("User" only)
    void *inst = Synth2Create("/tmp/oxo_s2_ios_harness_lib", nullptr, nullptr);
    Synth2Prepare(inst, 48000, 512);
    std::vector<float> L(512), R(512);
    AudioBufferList *abl = (AudioBufferList *)calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer));
    abl->mNumberBuffers = 2;
    abl->mBuffers[0] = {1, 512 * 4, L.data()};
    abl->mBuffers[1] = {1, 512 * 4, R.data()};
    // note on at offset 100 + a learned CC 74 -> filter freq
    OxoSynth2Hooks h = Synth2MakeHooks(inst);
    h.setMidiCC(h.ctx, 19, 74);
    AURenderEvent ev[2]{};
    ev[0].MIDI = {nullptr, 100, AURenderEventMIDI, 0, 3, 0, {0x90, 60, 100}};
    ev[1].MIDI = {nullptr, 200, AURenderEventMIDI, 0, 3, 0, {0xB0, 74, 127}};
    ev[0].MIDI.next = &ev[1];
    Synth2Render(inst, 512, abl, ev, 0, 120, 0, 0);
    float pre = 0, post = 0; // notes snap to the original's 16-sample grid: 100 -> 96
    for (int i = 0; i < 96; ++i) pre = std::max(pre, std::fabs(L[i]));
    for (int i = 96; i < 512; ++i) post = std::max(post, std::fabs(L[i]));
    expect(pre == 0 && post > 0.01f, "note sounds from its grid offset");
    expect(Synth2GetParam(inst, 19) == 127, "learned CC drives filter freq");
    expect((h.lastCC(h.ctx) & 0x7F) == 74, "last CC token");
    expect(h.voiceStates(h.ctx) == 2, "voice 0 held");
    ev[0].MIDI = {nullptr, 0, AURenderEventMIDI, 0, 3, 0, {0x80, 60, 0}};
    Synth2Render(inst, 512, abl, ev, 0, 120, 0, 0);
    expect(h.voiceStates(h.ctx) == 1, "voice 0 releasing");
    // menu > All notes off: every voice cut by the next render
    ev[0].MIDI = {nullptr, 0, AURenderEventMIDI, 0, 3, 0, {0x90, 64, 100}};
    ev[0].MIDI.next = nullptr;
    Synth2Render(inst, 512, abl, ev, 0, 120, 0, 0);
    expect(h.voiceStates(h.ctx) != 0, "note before all notes off");
    h.allNotesOff(h.ctx);
    Synth2Render(inst, 512, abl, nullptr, 0, 120, 0, 0);
    expect(h.voiceStates(h.ctx) == 0, "all notes off cuts every voice");
    // the editor reads / writes the volume (ID 99) through the hooks
    expect(h.getValue(h.ctx, 99) == 64, "editor reads the volume");
    h.setValue(h.ctx, 99, 80);
    expect(h.getValue(h.ctx, 99) == 80 && Synth2GetParam(inst, 99) == 80, "editor sets the volume");
    h.setValue(h.ctx, 99, 64);
    // state round trip
    Synth2SetParam(inst, 0, 3);
    h.setPatchName(h.ctx, "Harness");
    int32_t n = Synth2EncodeState(inst, nullptr, 0);
    std::vector<uint8_t> blob(n);
    Synth2EncodeState(inst, blob.data(), n);
    void *other = Synth2Create("/tmp/oxo_s2_ios_harness_lib", nullptr, nullptr);
    expect(Synth2RestoreState(other, blob.data(), n) == 1, "restore");
    expect(Synth2GetParam(other, 99) == 64, "default volume restored");
    expect(Synth2GetParam(other, 0) == 3 && Synth2GetParam(other, 19) == 127, "restored values");
    OxoSynth2Hooks h2 = Synth2MakeHooks(other);
    expect(std::strcmp(h2.patchName(h2.ctx), "Harness") == 0, "restored name");
    {   // the theme is per instance, in the session, untouched by patch loads
        expect(*h2.themeText(h2.ctx) == 0, "no theme before the editor sets one");
        h.setThemeText(h.ctx, "{\"panel\":\"#A71818\"}");
        const int32_t sz = Synth2EncodeState(inst, nullptr, 0);
        std::vector<uint8_t> st(sz);
        Synth2EncodeState(inst, st.data(), sz);
        const int32_t serial = h2.patchSerial(h2.ctx);
        expect(Synth2RestoreState(other, st.data(), sz) == 1 && std::strcmp(h2.themeText(h2.ctx), "{\"panel\":\"#A71818\"}") == 0 &&
                   h2.patchSerial(h2.ctx) != serial, "theme restored, serial bumped");
        h2.initializePatch(h2.ctx);
        expect(std::strcmp(h2.themeText(h2.ctx), "{\"panel\":\"#A71818\"}") == 0, "patch load keeps the theme");
        h2.setThemeText(h2.ctx, "{\"panel\":\"#8AA7B8\"}");
        expect(std::strcmp(h.themeText(h.ctx), "{\"panel\":\"#A71818\"}") == 0, "other instance keeps its theme");
        expect(Synth2RestoreState(other, blob.data(), n) == 1 && *h2.themeText(h2.ctx) == 0, "v4 state without theme");
    }
    // library through the hooks: write the patch into User slot 1, read it back
    expect(std::strstr(h.libraryJSON(h.ctx), "\"User\"") != nullptr, "library has User bank");
    expect(h.writeLibraryPatch(h.ctx, 0, 0, "Written") == 1, "write slot");
    expect(h2.loadLibraryPatch(h2.ctx, 0, 0) == 1 && std::strcmp(h2.patchName(h2.ctx), "Written") == 0, "load written");
    int32_t pb = -1, ps = -1;
    h2.loadedProgram(h2.ctx, &pb, &ps);
    expect(pb == 0 && ps == 0, "loaded program = bank 0 slot 0");
    {   // MIDI program change: slot of the current bank (inst wrote into User 001)
        Synth2SetParam(inst, 0, 1);
        h.setPatchName(h.ctx, "Tweaked");
        AURenderEvent pc{};
        pc.MIDI = {nullptr, 10, AURenderEventMIDI, 0, 2, 0, {0xC3, 0, 0}};
        // menu > Program change: off by default = ignored
        expect(h.programChange(h.ctx) == 0, "program change off by default");
        Synth2Render(inst, 512, abl, &pc, 0, 120, 0, 0);
        expect(Synth2GetParam(inst, 0) == 1, "program change off: ignored");
        h.setProgramChange(h.ctx, 1);
        void *later = Synth2Create("/tmp/oxo_s2_ios_harness_lib", nullptr, nullptr);
        OxoSynth2Hooks hl = Synth2MakeHooks(later);
        expect(hl.programChange(hl.ctx) == 1, "program change preference saved");
        Synth2Destroy(later);
        pc.MIDI.data[1] = 5; // empty slot 006: ignored
        Synth2Render(inst, 512, abl, &pc, 0, 120, 0, 0);
        expect(Synth2GetParam(inst, 0) == 1 && std::strcmp(h.patchName(h.ctx), "Tweaked") == 0, "program change to an empty slot");
        pc.MIDI.data[1] = 0;
        Synth2Render(inst, 512, abl, &pc, 0, 120, 0, 0);
        expect(Synth2GetParam(inst, 0) == 3 && Synth2GetParam(inst, 19) == 127, "program change loads slot 001");
        h.loadedProgram(h.ctx, &pb, &ps);
        expect(std::strcmp(h.patchName(h.ctx), "Written") == 0 && pb == 0 && ps == 0, "UI follows the program change");
        Synth2SetParam(inst, 0, 1);
        MIDIEventList pcList{};
        MIDIEventPacket *p2 = MIDIEventListInit(&pcList, kMIDIProtocol_2_0);
        UInt32 pcWords[2] = {0x40C00000, 0x00000000}; // MIDI 2.0 program change, program 0
        MIDIEventListAdd(&pcList, sizeof pcList, p2, 0, 2, pcWords);
        AURenderEvent ump2{};
        ump2.MIDIEventsList.eventType = AURenderEventMIDIEventList;
        ump2.MIDIEventsList.eventList = pcList;
        Synth2Render(inst, 512, abl, &ump2, 0, 120, 0, 0);
        expect(Synth2GetParam(inst, 0) == 3, "MIDI 2.0 program change");
        h.setProgramChange(h.ctx, 0);
        Synth2SetParam(inst, 0, 1);
        Synth2Render(inst, 512, abl, &pc, 0, 120, 0, 0);
        expect(Synth2GetParam(inst, 0) == 1, "program change switched off: ignored");
        Synth2SetParam(inst, 0, 3); // the state checks below expect it
    }
    {   // the session keeps the program; a fresh instance starts on the library's first patch
        const int32_t sz = Synth2EncodeState(other, nullptr, 0);
        std::vector<uint8_t> st(sz);
        Synth2EncodeState(other, st.data(), sz);
        void *third = Synth2Create("/tmp/oxo_s2_ios_harness_lib", nullptr, nullptr);
        OxoSynth2Hooks h3 = Synth2MakeHooks(third);
        h3.loadedProgram(h3.ctx, &pb, &ps);
        expect(pb >= 0 && ps >= 0 && *h3.patchName(h3.ctx) != 0, "fresh instance on the first library patch");
        h3.initializePatch(h3.ctx);
        h3.loadedProgram(h3.ctx, &pb, &ps);
        expect(pb == -1 && ps == -1, "Init clears the program");
        expect(Synth2RestoreState(third, st.data(), sz) == 1, "restore program state");
        h3.loadedProgram(h3.ctx, &pb, &ps);
        expect(pb == 0 && ps == 0 && std::strcmp(h3.patchName(h3.ctx), "Written") == 0, "session restores the program");
        Synth2Destroy(third);
    }
    expect(std::strstr(h.slotPath(h.ctx, 0, 0), "001.sy1") != nullptr && *h.slotPath(h.ctx, 0, 1) == 0, "slot path");
    // import: a written patch file comes back as the "Imported" bank
    expect(h.savePatchFile(h.ctx, "/tmp/oxo_s2_harness_import.sy1") == 1, "save patch file");
    expect(std::strstr(h.importItems(h.ctx, "/tmp/oxo_s2_harness_import.sy1"), "/Imported") != nullptr, "import");
    expect(std::strstr(h.libraryJSON(h.ctx), "\"Imported\"") != nullptr, "imported bank listed");
    // a zip put into the library: extracted on rescan, moved to the Trash
    std::system("rm -rf /tmp/oxo_s2_hz && mkdir -p /tmp/oxo_s2_hz/Z && cp /tmp/oxo_s2_harness_import.sy1 /tmp/oxo_s2_hz/Z/001.sy1"
                " && cd /tmp/oxo_s2_hz && zip -q -r /tmp/oxo_s2_ios_harness_lib/Dropped.zip Z");
    h.rescanLibrary(h.ctx);
    expect(std::strstr(h.libraryJSON(h.ctx), "\"Dropped\"") != nullptr, "dropped zip extracted");
    expect(std::strcmp(h.libraryNotice(h.ctx), "Dropped.zip imported and moved to Trash") == 0, "dropped zip notice");
    expect(*h.libraryNotice(h.ctx) == 0, "notice cleared");
    {   // Delete bank: the folder goes (here to the Trash: mac), the bank leaves the list
        const char *j = h.libraryJSON(h.ctx), *at = std::strstr(j, "\"name\":\"Dropped\"");
        int32_t b = -1;
        for (const char *p = j; at && (p = std::strstr(p, "\"name\":")) && p <= at; ++p) ++b;
        expect(b >= 0 && h.deleteBank(h.ctx, b) == 1, "delete bank");
        expect(std::strstr(h.libraryJSON(h.ctx), "\"Dropped\"") == nullptr, "deleted bank gone");
    }
    {   // Export bank: bank 0 (Imported) becomes a folder in the picked one
        std::system("rm -rf /tmp/oxo_s2_hx && mkdir -p /tmp/oxo_s2_hx");
        expect(std::strcmp(h.exportBank(h.ctx, 0, "/tmp/oxo_s2_hx"), "/tmp/oxo_s2_hx/Imported") == 0 &&
                   std::system("test -f /tmp/oxo_s2_hx/Imported/001.sy1") == 0, "export bank");
    }
    {   // background rescan (browser open): a bank added in Finder arrives through the revision poll
        std::system("mkdir -p '/tmp/oxo_s2_ios_harness_lib/Finder Bank' && cp /tmp/oxo_s2_harness_import.sy1 "
                    "'/tmp/oxo_s2_ios_harness_lib/Finder Bank/001.sy1'");
        const int32_t rev = h.libraryRevision(h.ctx);
        h.startLibraryScan(h.ctx);
        for (int i = 0; i < 2000 && (h.libraryScanning(h.ctx) || h.libraryRevision(h.ctx) == rev); ++i) usleep(1000);
        expect(h.libraryRevision(h.ctx) != rev && !h.libraryScanning(h.ctx) &&
                   std::strstr(h.libraryJSON(h.ctx), "\"Finder Bank\"") != nullptr, "background rescan adopted");
    }
    // drag onto a bank = copy into its first free slot (bank 0 = "Finder Bank", 001 taken)
    expect(h.copyPatch(h.ctx, 0, 0, 0) == 1 &&
               std::system("test -f '/tmp/oxo_s2_ios_harness_lib/Finder Bank/002.sy1'") == 0, "copy patch");
    // Initialize patch = Synth2's INIT; the double-click default follows it
    int32_t lo = 0, hi = 0, def = 0;
    h.paramInfo(h.ctx, 33, &lo, &hi, &def);
    h2.initializePatch(h2.ctx);
    expect(std::strcmp(h2.patchName(h2.ctx), "INIT") == 0 && Synth2GetParam(other, 33) == 14 && def == 14, "initialize");
    Synth2SetParam(inst, 99, 100);
    n = Synth2EncodeState(inst, nullptr, 0);
    blob.resize(n);
    Synth2EncodeState(inst, blob.data(), n);
    expect(Synth2RestoreState(other, blob.data(), n) == 1 && Synth2GetParam(other, 99) == 100, "volume in the session state");
    // a v1 chunk ('OXS2' + 1 + .sy1 text): default volume
    std::vector<uint8_t> v1 = {'O', 'X', 'S', '2', 1, 0, 0, 0};
    const auto u32At = [&](size_t at) { return blob[at] | blob[at + 1] << 8 | blob[at + 2] << 16 | size_t(blob[at + 3]) << 24; };
    const size_t themeAt = 20 + u32At(16), text = themeAt + 4 + u32At(themeAt); // v4 header
    v1.insert(v1.end(), blob.begin() + long(text), blob.end());
    expect(Synth2RestoreState(other, v1.data(), int32_t(v1.size())) == 1 && Synth2GetParam(other, 99) == 64 &&
               Synth2GetParam(other, 0) == 3, "v1 state");
    Synth2SetParam(inst, 99, 64);
    // a patch load leaves the volume alone
    Synth2SetParam(other, 99, 90);
    h2.initializePatch(h2.ctx);
    expect(Synth2GetParam(other, 99) == 90, "patch load keeps the volume");
    expect(Synth2ParamCount() == 100, "host params = patch + volume");
    // MIDI 2.0 UMP note-on
    MIDIEventList list{};
    MIDIEventPacket *pkt = MIDIEventListInit(&list, kMIDIProtocol_2_0);
    UInt32 words[2] = {0x40903C00, 0xFFFF0000};
    MIDIEventListAdd(&list, sizeof list, pkt, 0, 2, words);
    AURenderEvent ump{};
    ump.MIDIEventsList.eventSampleTime = 0;
    ump.MIDIEventsList.eventType = AURenderEventMIDIEventList;
    ump.MIDIEventsList.eventList = list;
    Synth2Prepare(other, 48000, 512);
    std::fill(L.begin(), L.end(), 0.f);
    Synth2Render(other, 512, abl, &ump, 0, 120, 0, 0);
    float umpPeak = 0;
    for (float v : L) umpPeak = std::max(umpPeak, std::fabs(v));
    expect(umpPeak > 0.01f, "MIDI 2.0 note sounds");
    Synth2Destroy(inst);
    Synth2Destroy(other);
    std::printf("%s\n", fails ? "harness FAILED" : "harness ok");
    return fails;
}
