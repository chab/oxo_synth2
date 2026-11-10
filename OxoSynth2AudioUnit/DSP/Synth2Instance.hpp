#pragma once

// One AUv3 instance (iPad): the engine (audio thread), lock-free parameter
// mailboxes, MIDI learn, and the editor services shared with the mac VST3
// controller (SynthEditorCore). The AU parameter tree is the host-facing
// truth for automation; the UI edits go through it via `editCallback`.

#include "EditorCore/State.hpp"
#include "EditorCore/Storage.hpp"
#include "EditorCore/SynthEditorCore.hpp"
#include "EngineCore/MidiLearn.hpp"
#include "EngineCore/Synth.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace oxo {

class Synth2Instance final : public SynthEditorCore {
public:
    // phase: 0 = begin gesture, 1 = value, 2 = end gesture
    using EditCallback = void (*)(void *ctx, int32_t id, int32_t value, int32_t phase);

    Synth2Instance(const std::string &libraryRoot, const std::string &factoryRoot, const std::string &build) {
        const s2::MidiLearnTable learned = storage::LoadMidiLearn();
        programChangeOn_ = storage::LoadProgramChange();
        for (int i = 0; i < s2::kParamCount; ++i) {
            values_[size_t(i)] = s2::kParams[size_t(i)].def;
            learnCc_[size_t(i)] = learned[size_t(i)];
        }
        dirty_[0] = dirty_[1] = ~uint64_t(0);
        ConfigureLibrary(libraryRoot, factoryRoot, build);
        // A fresh instance plays the library's first patch (Synth1: program
        // 001 of the first bank); a session's fullState replaces it.
        s2::Sy1Patch patch;
        s2::LibraryProgram where;
        if (StartupProgram(patch, where)) {
            for (int i = 0; i < s2::kParamCount; ++i) values_[size_t(i)] = s2::ClampValue(i, patch.values[size_t(i)]);
            RestoredPatch(patch.name, patch.color, where);
        }
    }

    void SetEditCallback(void *ctx, EditCallback cb) {
        editCtx_ = ctx;
        edit_ = cb;
    }

    // ---- SynthEditorCore (UI thread) ----
    int32_t NativeValue(int id) override {
        if (id == s2::kVolumeParamId) return volume_.load(std::memory_order_relaxed);
        return id >= 0 && id < s2::kParamCount ? values_[size_t(id)].load(std::memory_order_relaxed) : 0;
    }
    void BeginEdit(int id) override {
        if (edit_) edit_(editCtx_, id, 0, 0);
    }
    void PerformEdit(int id, int32_t value) override {
        SetFromHost(id, value);
        if (edit_) edit_(editCtx_, id, value, 1);
    }
    void EndEdit(int id) override {
        if (edit_) edit_(editCtx_, id, 0, 2);
    }
    int32_t MidiCC(int id) override { return id >= 0 && id < s2::kParamCount ? learnCc_[size_t(id)].load() : 0; }
    void SetMidiCC(int id, int32_t cc) override {
        if (!s2::IsLearnable(id) || cc < 0 || cc > 127) return;
        learnCc_[size_t(id)] = cc;
        s2::MidiLearnTable t{};
        for (int i = 0; i < s2::kParamCount; ++i) t[size_t(i)] = learnCc_[size_t(i)];
        storage::SaveMidiLearn(t);
    }
    int32_t LastCC() override { return lastCc_.load(std::memory_order_relaxed); }
    uint64_t VoiceStates() override { return voices_.load(std::memory_order_relaxed); }
    void AllNotesOff() override { panic_.store(true, std::memory_order_release); }
    void PatchLoading() override { clearDelay_.store(true, std::memory_order_release); }
    bool ProgramBankChanged(const s2::ProgramBankValues &values) override {
        programs_.Publish(std::make_unique<s2::ProgramBankValues>(values));
        return true;
    }
    bool ProgramChangeOn() override { return programChangeOn_.load(std::memory_order_relaxed); }
    void SetProgramChangeOn(bool on) override {
        if (on == ProgramChangeOn()) return;
        programChangeOn_.store(on, std::memory_order_relaxed);
        storage::SaveProgramChange(on);
    }
    // The render switched program (MIDI program change): follow it.
    void PollProgramChange() override {
        const int32_t token = programToken_.load(std::memory_order_acquire);
        if (token == seenProgramToken_) return;
        seenProgramToken_ = token;
        AdoptProgramChange(token & 127);
    }

    // Parameter tree observer / UI edit / state restore (any non-audio thread).
    void SetFromHost(int id, int32_t value) {
        if (id == s2::kVolumeParamId) {
            volume_.store(s2::ClampValue(id, value), std::memory_order_relaxed);
            volumeDirty_.store(true, std::memory_order_release);
            return;
        }
        if (id < 0 || id >= s2::kParamCount) return;
        values_[size_t(id)].store(s2::ClampValue(id, value), std::memory_order_relaxed);
        dirty_[size_t(id) / 64].fetch_or(uint64_t(1) << (id % 64), std::memory_order_release);
    }

    std::vector<uint8_t> EncodeState() {
        PollProgramChange(); // the session keeps a program the render switched to
        return vst3::EncodeState({CurrentPatch(), volume_.load(), Program(), Theme()});
    }
    bool RestoreState(const uint8_t *data, size_t size) {
        vst3::PluginState st;
        if (!vst3::DecodeState(data, size, st)) return false;
        const s2::Sy1Patch &p = st.patch;
        PatchLoading();
        SetFromHost(s2::kVolumeParamId, st.volume);
        for (int i = 0; i < s2::kParamCount; ++i) SetFromHost(i, p.values[size_t(i)]);
        RestoredTheme(st.theme);
        RestoredPatch(p.name, p.color, st.program);
        return true;
    }

    // ---- audio thread ----
    void Prepare(double sampleRate, uint32_t maxFrames) {
        synth_.SetSampleRate(sampleRate);
        synth_.Reset();
        scratchL_.assign(std::max<uint32_t>(maxFrames, 1), 0.0f);
        scratchR_.assign(std::max<uint32_t>(maxFrames, 1), 0.0f);
        dirty_[0] = dirty_[1] = ~uint64_t(0);
        volumeDirty_ = true;
    }

    void SetTransport(double bpm, double beat, bool playing) { synth_.SetTransport(bpm, beat, playing); }

    void ApplyDirty() {
        if (panic_.exchange(false, std::memory_order_acquire)) synth_.Panic();
        if (clearDelay_.exchange(false, std::memory_order_acquire)) { // a patch load, before its edits
            synth_.ClearDelay();
            synth_.BeginPatchChange();
        }
        if (volumeDirty_.exchange(false, std::memory_order_acquire)) synth_.SetVolume(volume_.load(std::memory_order_relaxed));
        for (int w = 0; w < 2; ++w) {
            uint64_t bits = dirty_[size_t(w)].exchange(0, std::memory_order_acquire);
            while (bits) {
                const int b = __builtin_ctzll(bits);
                bits &= bits - 1;
                const int id = w * 64 + b;
                if (id < s2::kParamCount) synth_.SetParam(id, values_[size_t(id)].load(std::memory_order_relaxed));
            }
        }
    }

    // Host automation inside the render (sample-accurate).
    void Automate(int id, double value) {
        if (id == s2::kVolumeParamId) {
            volume_.store(s2::ClampValue(id, int32_t(std::lround(value))), std::memory_order_relaxed);
            synth_.SetVolume(volume_.load(std::memory_order_relaxed));
            return;
        }
        if (id < 0 || id >= s2::kParamCount) return;
        const int32_t v = s2::ClampValue(id, int32_t(std::lround(value)));
        values_[size_t(id)].store(v, std::memory_order_relaxed);
        synth_.SetParam(id, v);
    }

    void Midi(uint8_t status, uint8_t d1, uint8_t d2) {
        switch (status & 0xF0) {
        case 0x90:
            if (d2) synth_.NoteOn(d1 & 127, d2 & 127);
            else synth_.NoteOff(d1 & 127);
            break;
        case 0x80: synth_.NoteOff(d1 & 127); break;
        case 0xB0: ControlChange(d1 & 127, d2 & 127); break;
        case 0xC0: ProgramChange(d1 & 127); break;
        case 0xE0: synth_.PitchBend((d1 & 127) | ((d2 & 127) << 7)); break;
        case 0xD0: synth_.ChannelPressure(d1 & 127); break;
        default: break;
        }
    }

    void BeginBlock(uint32_t frames) { synth_.BeginBlock(int(frames)); }
    void Render(float *left, float *right, int frames) {
        synth_.Render(left, right, frames);
        voices_.store(synth_.VoiceStates(), std::memory_order_relaxed);
    }
    float *ScratchL() { return scratchL_.data(); }
    float *ScratchR() { return scratchR_.data(); }
    uint32_t ScratchFrames() const { return uint32_t(scratchL_.size()); }

private:
    // MIDI learn (Oxo Simple's model, same as the VST3 processor): a learned
    // CC drives its parameters; every CC bumps the "a CC arrived" token.
    void ControlChange(int cc, int value) {
        synth_.ControlChange(cc, value);
        if (cc == 0) return;
        for (int p = 0; p < s2::kParamCount; ++p) {
            if (learnCc_[size_t(p)].load(std::memory_order_relaxed) != cc) continue;
            const int32_t native = s2::ScaledValueForCC(p, value);
            values_[size_t(p)].store(native, std::memory_order_relaxed);
            synth_.SetParam(p, native);
        }
        ccCounter_ = (ccCounter_ + 1) & 0x1FFF;
        lastCc_.store(int32_t((ccCounter_ << 7) | uint32_t(cc)), std::memory_order_relaxed);
    }

    // MIDI program change: that slot of the current bank (EngineCore/
    // ProgramChange.hpp), the UI side follows on its next poll.
    void ProgramChange(int program) {
        if (!programChangeOn_.load(std::memory_order_relaxed)) return;
        const s2::PatchValues *v = s2::SwitchProgram(synth_, programs_.Current(), program);
        if (!v) return;
        for (int p = 0; p < s2::kParamCount; ++p) values_[size_t(p)].store((*v)[size_t(p)], std::memory_order_relaxed);
        programCounter_ = (programCounter_ + 1) & 0x1FFF;
        programToken_.store(int32_t((programCounter_ << 7) | uint32_t(program)), std::memory_order_release);
    }

    s2::Synth synth_;
    std::array<std::atomic<int32_t>, s2::kParamCount> values_;
    std::array<std::atomic<uint64_t>, 2> dirty_;
    std::array<std::atomic<int>, s2::kParamCount> learnCc_;
    std::atomic<int32_t> lastCc_{0};
    std::atomic<uint64_t> voices_{0};
    std::atomic<int32_t> volume_{s2::kVolumeParam.def}; // master volume: state chunk, not the patch
    std::atomic<bool> volumeDirty_{true};
    std::atomic<bool> panic_{false}; // menu > All notes off, done by the next render
    std::atomic<bool> clearDelay_{false}; // a patch load (editor, fullState), ditto
    uint32_t ccCounter_ = 0;
    s2::ProgramMailbox programs_;          // the current bank's values (UI -> render)
    std::atomic<int32_t> programToken_{0}; // render -> UI: (counter << 7) | slot
    std::atomic<bool> programChangeOn_{false}; // menu > Program change (settings.json)
    uint32_t programCounter_ = 0;          // audio thread
    int32_t seenProgramToken_ = 0;         // UI thread
    std::vector<float> scratchL_, scratchR_;
    void *editCtx_ = nullptr;
    EditCallback edit_ = nullptr;
};

} // namespace oxo
