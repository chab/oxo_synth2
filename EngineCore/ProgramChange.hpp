#pragma once

// MIDI program change: program 0..127 = slot 001..128 of the CURRENT bank
// (the bank of the last library program loaded; Init / a patch file keep
// it). The UI side reads that bank (ReadProgramBank) and hands its values to
// the audio thread (ProgramMailbox), which switches at the event's offset by
// itself (editor closed too) and reports the slot back; the UI side then
// follows (name, LCD, session program).

#include "PatchLibrary.hpp"

#include <atomic>
#include <cstdio>
#include <memory>

namespace oxo::s2 {

// What the audio thread needs: each slot's values (clamped), plain data.
struct ProgramBankValues {
    std::array<uint8_t, kBankSlots> present{};
    std::array<PatchValues, kBankSlots> values{};
};

// The UI side's copy: the patches (names for the LCD / host program list).
struct ProgramBankPatches {
    std::array<bool, kBankSlots> present{};
    std::array<Sy1Patch, kBankSlots> patches;

    ProgramBankValues Values() const {
        ProgramBankValues v;
        for (size_t s = 0; s < size_t(kBankSlots); ++s) v.present[s] = present[s], v.values[s] = patches[s].values;
        return v;
    }
};

// `bank` = a LibraryProgram::bank token (kFactoryProgramBank or a folder
// relative to `root`). A missing / unreadable bank = every slot empty.
inline ProgramBankPatches ReadProgramBank(const std::filesystem::path &root, const std::filesystem::path &factory,
                                   const std::string &bank) {
    ProgramBankPatches out;
    PatchLibrary lib;
    if (bank == kFactoryProgramBank) lib.ScanBank(factory);
    else if (!root.empty()) lib.ScanBank(BankAbsolute(root, bank));
    if (lib.Banks().empty()) return out;
    for (size_t s = 0; s < size_t(kBankSlots); ++s) {
        Sy1Patch &p = out.patches[s];
        if (!lib.Load(0, s, p)) continue;
        for (int i = 0; i < kParamCount; ++i) p.values[size_t(i)] = ClampValue(i, p.values[size_t(i)]);
        out.present[s] = true;
    }
    return out;
}

// One writer thread hands objects to the audio thread: no lock, no
// allocation or free on the audio side. The writer frees what the audio
// thread let go of (and a published object it never picked up).
template <class T> class RtMailbox {
public:
    RtMailbox() = default;
    RtMailbox(const RtMailbox &) = delete;
    RtMailbox &operator=(const RtMailbox &) = delete;
    ~RtMailbox() {
        delete next_.load();
        delete retired_.load();
        delete current_;
    }

    // Writer (one thread at a time, never the audio thread).
    void Publish(std::unique_ptr<T> object) {
        delete next_.exchange(object.release(), std::memory_order_acq_rel);
        delete retired_.exchange(nullptr, std::memory_order_acquire);
    }

    // Audio thread: the latest object published (nullptr = none yet). A new
    // one waits while the previous one is still to be freed by the writer
    // (which the next Publish does first thing after handing it over).
    const T *Current() {
        if (next_.load(std::memory_order_relaxed) && !retired_.load(std::memory_order_acquire))
            if (T *n = next_.exchange(nullptr, std::memory_order_acquire)) {
                retired_.store(current_, std::memory_order_release);
                current_ = n;
            }
        return current_;
    }

private:
    std::atomic<T *> next_{nullptr}, retired_{nullptr};
    T *current_ = nullptr; // audio thread only
};

using ProgramMailbox = RtMailbox<ProgramBankValues>;

// Audio thread: `synth` switches to `program` of `bank` like a browser load
// (the delay empties, the patch lands at silence, notes keep playing).
// Returns the values now playing, nullptr = no such patch (ignored).
template <class SynthT> const PatchValues *SwitchProgram(SynthT &synth, const ProgramBankValues *bank, int program) {
    if (!bank || program < 0 || program >= kBankSlots || !bank->present[size_t(program)]) return nullptr;
    synth.ClearDelay();
    synth.BeginPatchChange();
    synth.SetPatch(bank->values[size_t(program)]);
    return &bank->values[size_t(program)];
}

// "NNN:name" (the LCD's form), for a host's program list.
inline std::string ProgramLabel(const ProgramBankPatches &bank, int slot) {
    char n[8];
    std::snprintf(n, sizeof n, "%03d:", slot + 1);
    return n + (bank.present[size_t(slot)] ? bank.patches[size_t(slot)].name : std::string("----"));
}

} // namespace oxo::s2
