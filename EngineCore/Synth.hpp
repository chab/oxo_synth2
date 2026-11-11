#pragma once

// Oxo Synth2 engine: a Synth1 (Daichi) recreation, tuned against renders of
// the original (tools/s1ref). Header-only, deterministic (seeded PRNG), no
// allocation on the audio thread after SetSampleRate.

#include "Effects.hpp"
#include "Lfo.hpp"
#include "Params.hpp"
#include "Voice.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>

namespace oxo::s2 {

inline constexpr int kMaxVoices = 32;

// (measured) the original starts / ends notes at the start of their
// 16-sample sub-block, counted from the host block's start (up to 15
// samples early); bend / CC stay exact. Offset in the host block -> the
// offset to play a note on / off at.
constexpr int NoteEventOffset(int offset) { return offset & ~15; }

class Synth {
public:
    Synth() {
        for (int i = 0; i < kMaxVoices; ++i) voices_[i].InitSlot(i);
        SetPatch(DefaultValues());
        SetSampleRate(44100.0);
    }

    void SetSampleRate(double sr) {
        sampleRate_ = sr > 0 ? sr : 44100.0;
        for (Voice &v : voices_) v.Prepare(sampleRate_);
        delay_.Prepare(sampleRate_);
        chorus_.Prepare(sampleRate_);
        eq_.Prepare(sampleRate_);
        effect_.Prepare(sampleRate_);
    }
    double SampleRate() const { return sampleRate_; }

    void SetPatch(const PatchValues &values) {
        for (int i = 0; i < kParamCount; ++i) SetParam(i, values[i]);
    }
    // base_ = the patch; values_ = what the engine plays (base_ + MIDI ctrl
    // routing offsets, see ApplyRouting).
    void SetParam(int id, int32_t value) {
        if (id < 0 || id >= kParamCount) return;
        FlushNoteOns();
        if (switch_ == Switch::Out) { // patch switch: lands at silence
            pending_[id] = value;
            pendingMask_[size_t(id)] = true;
            switchQuiet_ = 0;
            return;
        }
        if (switch_ == Switch::Hold) switchQuiet_ = 0;
        const bool arpWas = values_[kArpOn] != 0;
        base_[id] = values_[id] = ClampValue(id, value);
        ApplyRouting();
        if (id == kArpOn && arpWas != (values_[kArpOn] != 0)) ArpSwitched();
    }
    int32_t Param(int id) const { return base_[id]; }
    const PatchValues &Values() const { return base_; }
    int32_t EffectiveParam(int id) const { return values_[id]; }
    int GatedVoices() const {
        int n = 0;
        for (const Voice &v : voices_) n += v.Active() && v.Gate();
        return n;
    }
    // Voice activity for the panel's grid: 2 bits per voice (bits 2i, 2i+1),
    // kVoiceIdle / kVoiceReleasing / kVoiceHeld.
    static constexpr uint64_t kVoiceIdle = 0, kVoiceReleasing = 1, kVoiceHeld = 2;
    uint64_t VoiceStates() const {
        uint64_t s = 0;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices_[i].Active()) s |= (voices_[i].Gate() ? kVoiceHeld : kVoiceReleasing) << (2 * i);
        return s;
    }

    void SetTransport(double bpm, double ppq, bool playing) {
        tempo_ = bpm > 0 ? bpm : 120.0;
        ppq_ = ppq;
        playing_ = playing;
    }

    void NoteOn(int key, int velocity) {
        if (velocity <= 0) return NoteOff(key);
        key &= 127;
        keyDown_[key] = true;
        keyVelocity_[key] = uint8_t(std::clamp(velocity, 1, 127));
        sustained_[key] = false;
        if (values_[kArpOn]) return ArpNoteOn(key, velocity);
        // (measured: free-phase chords nulled at +2 dB, -50 dB reversed)
        // poly notes in one 16-sample sub-block (the original's event grid)
        // take their voices in REVERSE order: the last one gets the next
        // round-robin slot. Mono / legato keep the arrival order (last note
        // wins, measured). Queued until the next render or other event.
        if (values_[kPlayMode] != 0 || noteOnCount_ == int(noteOns_.size())) FlushNoteOns();
        if (values_[kPlayMode] != 0) return VoiceNoteOn(key, velocity);
        noteOns_[size_t(noteOnCount_++)] = {uint8_t(key), uint8_t(keyVelocity_[key])};
    }

    // Sustain pedal (CC 64): a key released while the pedal is down is held
    // (arpeggiator included) until the pedal comes up.
    void NoteOff(int key) {
        FlushNoteOns();
        key &= 127;
        keyDown_[key] = false;
        if (sustain_) {
            sustained_[key] = true;
            return;
        }
        ReleaseKey(key);
    }

    void SetSustain(bool down) {
        if (sustain_ == down) return;
        sustain_ = down;
        if (down) return;
        for (int k = 0; k < 128; ++k)
            if (sustained_[k]) {
                sustained_[k] = false;
                if (!keyDown_[k]) ReleaseKey(k);
            }
    }

private:
    // A patch change switched the arp while keys are down (the browser keeps
    // notes playing): on = the held keys become the arp's (it arpeggiates
    // them), off = the arp's keys play their own voices again. Without this
    // a key would be released by the wrong path and stick.
    void FlushNoteOns() {
        while (noteOnCount_ > 0) {
            const auto [key, velocity] = noteOns_[size_t(--noteOnCount_)];
            VoiceNoteOn(key, velocity);
        }
    }

    void ArpSwitched() {
        if (values_[kArpOn]) {
            for (Voice &v : voices_)
                if (v.Active() && v.Gate()) v.Release();
            heldCount_ = 0;
            for (int k = 0; k < 128; ++k)
                if (keyDown_[k] || sustained_[k]) ArpNoteOn(k, keyVelocity_[k]);
            return;
        }
        int keys[32];
        const int count = arpCount_;
        std::copy(arpKeys_, arpKeys_ + count, keys);
        if (arpPlaying_ >= 0) VoiceNoteOff(arpPlaying_);
        arpPlaying_ = -1;
        arpCount_ = 0;
        for (int i = 0; i < count; ++i) VoiceNoteOn(keys[i], keyVelocity_[keys[i]]);
    }

    void ReleaseKey(int key) {
        const bool arpKey = std::find(arpKeys_, arpKeys_ + arpCount_, key) != arpKeys_ + arpCount_;
        if (arpKey) ArpNoteOff(key);
        if (values_[kArpOn] && arpKey) return; // the arp plays it
        // arp off, or a key pressed before a patch change switched the arp on:
        // it plays its own voice (left alone if the arp happens to play that note)
        if (!values_[kArpOn] || key != arpPlaying_) VoiceNoteOff(key);
    }

    void KeySyncRestart() {
        if (values_[kLfo1KeySync]) lfo_[0].Restart();
        if (values_[kLfo2KeySync]) lfo_[1].Restart();
    }

    // (measured) key sync restarts the LFOs when no key was held; arp steps
    // (`keySync` false) never do, the key starting the arp phrase does.
    void VoiceNoteOn(int key, int velocity, bool keySync = true) {
        bool anyGate = false;
        for (const Voice &v : voices_) anyGate |= v.Active() && v.Gate();
        if (keySync && !anyGate) KeySyncRestart();
        const int mode = values_[kPlayMode]; // 0 poly, 1 mono, 2 legato
        const bool porta = values_[kPortaTime] > 0;
        // The held-key stack follows every key in EVERY mode: a patch change
        // with keys down (the browser keeps notes playing) must not leave a
        // released key on it, or mono falls back to it = a stuck note.
        RemoveHeld(key);
        held_[heldCount_ < 32 ? heldCount_++ : 31] = key;
        if (mode != 0) {
            // Mono / legato: the held-key stack decides; one voice (or one
            // unison stack) keeps sounding; mono retriggers, legato glides (measured).
            const bool legatoPlay = anyGate;
            if (anyGate) {
                const bool glide = porta && (!values_[kPortaAuto] || legatoPlay);
                for (Voice &v : voices_)
                    if (v.Active()) v.ChangeNote(key, velocity, mode == 1, glide);
                if (mode == 1) RetriggerPhases();
                return;
            }
        } else {
            // (measured, poly) a key pressed again while held: its voice (stack)
            // is released and the note starts on a fresh voice
            for (Voice &v : voices_)
                if (v.Active() && v.Gate() && v.Note() == key) v.Release();
        }
        // (measured, measure_porta.py) a fresh note glides from its voice's
        // own last pitch (auto mode: never); mono / legato always use voice
        // slot 0 (unison: 0..N-1), poly the round robin.
        const bool glide = porta && !values_[kPortaAuto];
        if (!values_[kUnisonOn]) {
            // (measured) a poly chord over the polyphony: the notes processed
            // last (= its first ones, see NoteOn) find no voice and are dropped,
            // voices started in the same sub-block are never stolen
            Voice *v = mode != 0 ? &MonoVoice(0) : AllocateVoice(true);
            if (v) v->Start(key, velocity, glide, values_, now_);
            return;
        }
        // (measured) unison: N voices evenly over +-D cents, pan spread
        // (64 = mono, 0/127 = hard opposite), odd voices shifted by pitch - 24.
        const int n = std::clamp<int>(values_[kUnisonVoices], 2, 8);
        const double d = tables::kUnisonDetuneCents[values_[kUnisonDetune]];
        // (measured, check_unison.py) the lowest voice goes right below 64
        const double spread = (values_[kUnisonPan] - 64) / 64.0;
        const uint64_t age = now_;
        // (measured) unison phase shift (osc phase > 0; with 0 the slots'
        // phases carry on): the k-th voice started shifts each oscillator by
        // v / 127 of its slot k phase minus slot 0's (Voice::StartPhases).
        const double shift = values_[kUnisonPhase] / 127.0;
        // (measured, check_unison.py + chord slot orders) a note takes
        // min(voices, polyphony) voices (round robin as usual); a stack DOES
        // steal from a note started in the same sub-block (oldest first): a
        // chord over the polyphony keeps its first notes because they are
        // processed last (see NoteOn); a voice's place in the stack (detune,
        // pan, odd = pitch) is FIXED by its slot: slot % voices (a stack may
        // hold a place twice); the phase shift follows k.
        const int sounding = std::min(n, std::clamp<int>(values_[kPolyphony], 1, kMaxVoices));
        for (int k = 0; k < sounding; ++k) {
            Voice *slot = mode != 0 ? &MonoVoice(k) : AllocateVoice();
            if (!slot) break;
            Voice &v = *slot;
            const int i = lastVoice_ % n;
            const double t = n > 1 ? 2.0 * i / (n - 1) - 1.0 : 0.0;
            v.Start(key, velocity, glide, values_, age, t * d, t * spread,
                    (i & 1) ? values_[kUnisonPitch] - 24 : 0, k, shift);
        }
    }

    // (measured, check_unison.py) a mono retrigger restarts the oscillator
    // phases like a new note (osc phase > 0; a stack keeps its offsets).
    void RetriggerPhases() {
        for (Voice &v : voices_)
            if (v.Active() && v.Gate()) v.StartPhases(values_);
    }

    void RemoveHeld(int key) {
        int n = 0;
        for (int i = 0; i < heldCount_; ++i)
            if (held_[i] != key) held_[n++] = held_[i];
        heldCount_ = n;
    }

    void VoiceNoteOff(int key) {
        RemoveHeld(key);
        if (values_[kPlayMode] != 0) {
            const int n = heldCount_;
            if (n > 0) { // fall back to the previous held key
                // (measured, measure_mono.py) mono retriggers the envelopes here
                // (velocity of the last note kept), legato does not
                const int back = held_[n - 1];
                bool changed = false;
                for (Voice &v : voices_)
                    if (v.Active() && v.Note() == key) {
                        v.ChangeNote(back, -1, values_[kPlayMode] == 1, values_[kPortaTime] > 0);
                        changed = true;
                    }
                if (changed && values_[kPlayMode] == 1) RetriggerPhases();
                return;
            }
            for (Voice &v : voices_)
                if (v.Active() && v.Gate()) v.Release();
            return;
        }
        for (Voice &v : voices_)
            if (v.Active() && v.Gate() && v.Note() == key) v.Release();
    }

    // ---- arpeggiator (measured: step = tempo division, gate = gate/127 of
    // the step; types 1 updown 2 up 3 down 4 random; range 1..4 octaves).
    // (measured, measure_beat_lock.py) NOT locked to the host beat: steps
    // start at the key press. TODO(measure): random sequence.
    void ArpNoteOn(int key, int velocity) {
        if (arpCount_ == 0) {
            arpStep_ = -1;
            arpCountdown_ = 0;
            arpDir_ = 1;
            KeySyncRestart();
        }
        if (std::find(arpKeys_, arpKeys_ + arpCount_, key) != arpKeys_ + arpCount_) return;
        if (arpCount_ < 32) arpKeys_[arpCount_++] = key;
        std::sort(arpKeys_, arpKeys_ + arpCount_);
        arpVelocity_ = velocity;
    }
    void ArpNoteOff(int key) {
        int n = 0;
        for (int i = 0; i < arpCount_; ++i)
            if (arpKeys_[i] != key) arpKeys_[n++] = arpKeys_[i];
        arpCount_ = n;
        if (n == 0 && arpPlaying_ >= 0) {
            VoiceNoteOff(arpPlaying_);
            arpPlaying_ = -1;
        }
    }
    int ArpNextKey() {
        const int octaves = values_[kArpOctRange] + 1;
        const int total = arpCount_ * octaves;
        const int type = values_[kArpType];
        if (type == 4) {
            arpStep_ = int(rng_.NextU32() % uint32_t(total));
        } else if (type == 3) {
            arpStep_ = arpStep_ <= 0 ? total - 1 : arpStep_ - 1;
        } else if (type == 2) {
            arpStep_ = (arpStep_ + 1) % total;
        } else {
            if (total == 1) arpStep_ = 0;
            else {
                if (arpStep_ + arpDir_ >= total || arpStep_ + arpDir_ < 0) arpDir_ = -arpDir_;
                arpStep_ = std::clamp(arpStep_ + arpDir_, 0, total - 1);
            }
        }
        return arpKeys_[arpStep_ % arpCount_] + 12 * (arpStep_ / arpCount_);
    }
    // Samples until the next arp event (step or gate-off), INT_MAX if idle.
    int ArpAdvance(int elapsed) {
        if (!values_[kArpOn] || (arpCount_ == 0 && arpPlaying_ < 0)) return 1 << 30;
        arpCountdown_ -= elapsed;
        arpGateCountdown_ -= elapsed;
        if (arpPlaying_ >= 0 && arpGateCountdown_ <= 0 && values_[kArpGate] < 127) {
            VoiceNoteOff(arpPlaying_);
            arpPlaying_ = -1;
        }
        if (arpCountdown_ <= 0 && arpCount_ > 0) {
            const double step = kArpBeats[std::clamp<int>(values_[kArpBeat], 0, 18)] * 60.0 / tempo_ * sampleRate_;
            arpAccum_ += step;
            const int stepSamples = std::max(1, int(arpAccum_));
            arpAccum_ -= stepSamples;
            if (arpPlaying_ >= 0) VoiceNoteOff(arpPlaying_);
            arpPlaying_ = ArpNextKey();
            if (values_[kArpGate] > 0) VoiceNoteOn(arpPlaying_, arpVelocity_, false); // (measured) gate 0 = silent
            else arpPlaying_ = -1;
            arpCountdown_ += stepSamples;
            arpGateCountdown_ = std::max(1, int(stepSamples * values_[kArpGate] / 127.0));
        }
        int next = arpCountdown_;
        if (arpPlaying_ >= 0 && arpGateCountdown_ > 0) next = std::min(next, arpGateCountdown_);
        return std::max(1, next);
    }

public:
    void ControlChange(int cc, int value) {
        FlushNoteOns();
        cc &= 127;
        cc_[cc] = std::clamp(value, 0, 127);
        if (cc == 64) SetSustain(value >= 64);
        else if (cc == 120 || cc == 123) AllNotesOff(cc == 120);
        ApplyRouting();
    }
    void PitchBend(int value14) {
        FlushNoteOns();
        bend_ = value14 - 8192;
        ApplyRouting();
    }
    void ChannelPressure(int value) {
        FlushNoteOns();
        pressure_ = std::clamp(value, 0, 127);
        ApplyRouting();
    }

    // Menu > All notes off: every voice cut at once, every key / pedal /
    // arp state forgotten (a stuck note can't survive it).
    void Panic() {
        AllNotesOff(true);
        keyDown_.fill(false);
        sustain_ = false;
    }

    void AllNotesOff(bool immediate) {
        noteOnCount_ = 0;
        for (Voice &v : voices_) immediate ? v.Kill() : v.Release();
        sustained_.fill(false);
        heldCount_ = 0;
        arpCount_ = 0;
        arpPlaying_ = -1;
    }

    // (measured, reference/meta/wheel.json, exact on 418 points) MIDI ctrl
    // routing: src word (status << 8 | data1: 0xB0nn CC nn, 0xE000 bend,
    // 0xD000 channel pressure) adds round(v * d * 128 / (63 * 127)) knob
    // units to the assigned parameter, v = controller 0..127, d = sens - 64
    // above 64, sens - 63 below 63 (63 / 64 = off). (measured,
    // measure_ctrl_span.py) targets whose range is not 0..127 (key shift,
    // bend range, unison pitch, ...) are NOT routed by the original.
    static int RouteOffset(double v, int sens, int span) {
        const int d = sens >= 64 ? sens - 64 : sens <= 62 ? sens - 63 : 0;
        const double x = double(v) * d * (span + 1) / (63.0 * 127.0);
        return int(x >= 0 ? std::floor(x + 0.5) : -std::floor(-x + 0.5));
    }

    void Reset() {
        noteOnCount_ = 0;
        for (Voice &v : voices_) v.Kill();
        heldCount_ = 0;
        arpCount_ = 0;
        arpPlaying_ = -1;
        delay_.Reset();
        chorus_.Reset();
        eq_.Reset();
        effect_.Reset();
        bend_ = 0;
        pressure_ = 0;
        cc_.fill(0);
        sustain_ = false;
        sustained_.fill(false);
        keyDown_.fill(false);
        ApplyRouting();
    }

    // A patch load (editor, host state): the delay empties with a short fade,
    // so the switch's transient doesn't ring in a long feedback.
    void ClearDelay() { delay_.Clear(); }

    // A patch switch (browser / editor load, BEFORE its parameter edits):
    // the voice mix (pre effects) fades out, the edits made meanwhile land
    // at silence with the voices' filters cleared (no half-switched or
    // resonating states), then it fades back in. Notes keep playing.
    void BeginPatchChange() {
        if (switch_ == Switch::None || switch_ == Switch::In) switch_ = Switch::Out;
        switchQuiet_ = 0;
    }
    bool PatchChanging() const { return switch_ != Switch::None; }

    void Render(float *left, float *right, int frames) {
        if (frames > kMaxRender) { // (the LFO pan gains' buffer)
            for (int pos = 0; pos < frames; pos += kMaxRender)
                Render(left + pos, right + pos, std::min(kMaxRender, frames - pos));
            return;
        }
        FlushNoteOns();
        const simd::ScopedNoDenormals noDenormals;
        std::fill(left, left + frames, 0.0f);
        std::fill(right, right + frames, 0.0f);
        // (measured, measure_pitch_mod.py) only the bend's 7-bit MSB counts:
        // trunc((v - 8192) / 128) clamped to +-63, / 63 = the full range
        const double bendSemis = std::clamp(bend_ / 128, -63, 63) / 63.0 * values_[kBendRange];
        int toArp = ArpAdvance(0);
        bool panLfo = false;
        for (int pos = 0, n = 0; pos < frames; pos += n) {
            n = std::min({kLfoBlock, frames - pos, toArp});
            const double *lfo[2] = {nullptr, nullptr};
            const int32_t *lfoSteps[2] = {nullptr, nullptr};
            for (int k = 0; k < 2; ++k) {
                // LFOs run even when off so their phase stays free-running;
                // (measured) tempo-synced LFOs are NOT locked to the host beat.
                const int speed = values_[k ? kLfo2Speed : kLfo1Speed];
                const double hz = values_[k ? kLfo2TempoSync : kLfo1TempoSync] ? tempo_ / 60.0 / Lfo::SyncBeats(speed)
                                                                                 : Lfo::SpeedHz(speed);
                // (an LFO on pitch: its own whole steps, see Lfo.hpp)
                const int dest = values_[k ? kLfo2Dest : kLfo1Dest];
                const bool pitch = values_[k ? kLfo2On : kLfo1On] && (dest <= 2);
                lfo_[k].Render(lfoBuf_[k], n, values_[k ? kLfo2Type : kLfo1Type], hz, sampleRate_,
                               pitch ? lfoSteps_[k] : nullptr, Lfo::PitchSteps(values_[k ? kLfo2Depth : kLfo1Depth]),
                               dest == 4);
                if (values_[k ? kLfo2On : kLfo1On]) lfo[k] = lfoBuf_[k];
                lfoSteps[k] = pitch ? lfoSteps_[k] : nullptr;
            }
            double cutoffStart = 0;
            const double cutoffStep = CutoffRamp(n, cutoffStart);
            for (Voice &v : voices_)
                v.Render(left + pos, right + pos, n, blockPos_ + pos, blockEnd_, values_, bendSemis, lfo, lfoSteps, cutoffStart,
                         cutoffStep);
            if (switch_ != Switch::None) SwitchFade(left + pos, right + pos, n);
            panLfo = LfoPanGains(pos, n, lfo);
            toArp = ArpAdvance(n);
        }
        // (measured, measure_fx_order.py) voices -> effect -> pan -> EQ -> delay -> chorus
        if (values_[kEffectOn]) effect_.Process(left, right, frames, values_);
        Pan(left, right, frames, panLfo);
        eq_.Process(left, right, frames, values_);
        if (values_[kDelayOn] && values_[kChorusOn]) {
            // (measured) the echoes reach the output through the chorus only
            for (int pos = 0; pos < frames; pos += kEchoChunk) {
                const int n = std::min(kEchoChunk, frames - pos);
                delay_.Process(left + pos, right + pos, n, values_, tempo_, echoL_, echoR_);
                chorus_.Process(left + pos, right + pos, n, values_, echoL_, echoR_);
            }
        } else {
            if (values_[kDelayOn]) delay_.Process(left, right, frames, values_, tempo_);
            else delay_.ClearNow();
            if (values_[kChorusOn]) chorus_.Process(left, right, frames, values_);
            else chorus_.Advance(frames, values_);
        }
        if (volume_ != 1.0 || volumeTarget_ != 1.0) { // master volume, linear ramp over the block
            const double step = frames > 0 ? (volumeTarget_ - volume_) / frames : 0.0;
            for (int i = 0; i < frames; ++i) {
                volume_ += step;
                left[i] = float(left[i] * volume_);
                right[i] = float(right[i] * volume_);
            }
        }
        volume_ = volumeTarget_;
        now_ += uint64_t(std::max(frames, 0));
        blockPos_ += std::max(frames, 0);
    }

    // A host block of `frames` samples starts (before its first Render): the
    // original's voice end grid (256 samples) counts from there, like
    // NoteEventOffset, and also ends at the block's end.
    void BeginBlock(int frames) {
        blockPos_ = 0;
        blockEnd_ = std::max(frames, 1);
    }

    // Master volume (kVolumeParamId, not a patch parameter): survives Reset.
    void SetVolume(int32_t v) { volumeTarget_ = VolumeGain(ClampValue(kVolumeParamId, v)); }

private:
    // (measured, measure_ctrl_sources.py) CC / pressure 0..127; pitch bend is
    // BIPOLAR: (bend - 8192) / 64, -128 .. +128 (centre = no offset).
    double SourceValue(int word) const {
        switch ((word >> 8) & 0xF0) {
        case 0xB0: return cc_[word & 0x7F];
        case 0xE0: return bend_ / 64.0;
        case 0xD0: return pressure_;
        default: return 0;
        }
    }

    void ApplyRouting() {
        for (int t : routed_)
            if (t >= 0) values_[t] = base_[t];
        static constexpr int kSrc[2] = {kCtrlSrc1, kCtrlSrc2}, kDst[2] = {kCtrlAssign1, kCtrlAssign2},
                             kSens[2] = {kWheelSpeedSens, kWheelAmountSens};
        for (int r = 0; r < 2; ++r) {
            const int t = base_[kDst[r]];
            routed_[r] = -1;
            if (t < 0 || t >= kParamCount || kParams[t].min != 0 || kParams[t].max != 127) continue;
            const int off = RouteOffset(SourceValue(base_[kSrc[r]]), base_[kSens[r]], kParams[t].max - kParams[t].min);
            routed_[r] = t;
            values_[t] = ClampValue(t, values_[t] + off);
        }
    }

    // (measured, cutoff jumps through the AU parameter at 44.1 / 96 kHz) the
    // cutoff knob GLIDES to a new value: a linear ramp over 50 ms whatever
    // the step (40 -> 41 and 40 -> 100 alike), both ways. Without it, fast
    // mouse moves step audibly. Returns the per-sample step for the next n
    // samples (`start` = the knob at the block start) and moves cutoff_ to
    // the block end; snaps while no voice sounds (nothing to hear).
    static constexpr double kCutoffGlideSeconds = 0.05;
    double CutoffRamp(int n, double &start) {
        const double target = values_[kFilterFreq];
        if (target != cutoffTarget_) {
            cutoffTarget_ = target;
            cutoffRate_ = std::abs(target - cutoff_) / (kCutoffGlideSeconds * sampleRate_);
        }
        const bool sounding = std::any_of(voices_.begin(), voices_.end(), [](const Voice &v) { return v.Active(); });
        if (!sounding) cutoff_ = cutoffTarget_;
        start = cutoff_;
        const double left = cutoffTarget_ - cutoff_;
        if (std::abs(left) <= cutoffRate_ * n) { // lands within this block
            cutoff_ = cutoffTarget_;
            return left / n;
        }
        const double step = left > 0 ? cutoffRate_ : -cutoffRate_;
        cutoff_ += step * n;
        return step;
    }

    // (measured, pan with each effect type, delay / chorus types that cross
    // the channels) the patch pan and the LFOs on pan act on the voice mix
    // AFTER the effect, before the delay and chorus: an effect sees the
    // centred mix, both channels = its output times the pan gains (only the
    // unison spread pans before it, Voice). Patch pan q = (pan - 64) / 64
    // below the centre, / 63 above, balance gains (centre 1/1, hard side
    // 1/0). LFO -> position q = 1 - (1 - s)^3 for s = depth / 127 * lfo >= 0
    // (odd, + = left, both LFOs: s = the depth-weighted sum, unclamped:
    // 127 + 127 -> gains 2 / -1), gains toward side 1 + |q| / 2, away side
    // 1 - |q|; the two gains multiply.
    // The LFOs' gains for the chunk at pos (the effect runs on the whole
    // block first); false = no LFO on pan.
    bool LfoPanGains(int pos, int n, const double *const lfo[2]) {
        const double *v[2];
        double scale[2];
        int routes = 0;
        for (int k = 0; k < 2; ++k)
            if (Voice::LfoDest(values_, lfo, k) == 7) v[routes] = lfo[k], scale[routes++] = values_[k ? kLfo2Depth : kLfo1Depth] / 127.0;
        for (int i = 0; i < n && routes; ++i) {
            double s = 0;
            for (int k = 0; k < routes; ++k) s += scale[k] * v[k][i];
            // (the cube by hand: a pow call here, even never taken, cost the loop ~40 instructions)
            const double u = 1.0 - std::fabs(s), cube = u * u * u;
            const double q = s >= 0 ? 1.0 - cube : cube - 1.0;
            panGain_[0][pos + i] = float(q >= 0 ? 1.0 + 0.5 * q : 1.0 + q);
            panGain_[1][pos + i] = float(q >= 0 ? 1.0 - q : 1.0 - 0.5 * q);
        }
        return routes > 0;
    }
    void Pan(float *left, float *right, int n, bool lfo) const {
        const double q = (values_[kPan] - 64) / (values_[kPan] < 64 ? 64.0 : 63.0);
        const double pl = std::min(1.0, 1.0 - q), pr = std::min(1.0, 1.0 + q);
        if (!lfo && q == 0) return;
        for (int i = 0; i < n; ++i) {
            left[i] = float(left[i] * (lfo ? pl * panGain_[0][i] : pl));
            right[i] = float(right[i] * (lfo ? pr * panGain_[1][i] : pr));
        }
    }

    // Patch switch fade on one render chunk: ramp x (gain = smoothstep(x)),
    // stage changes at chunk ends.
    void SwitchFade(float *left, float *right, int n) {
        static constexpr double kOutSec = 0.025, kInSec = 0.25, kQuietSec = 0.01, kMaxHoldSec = 0.15;
        const double step = switch_ == Switch::Out ? -1.0 / (kOutSec * sampleRate_)
                            : switch_ == Switch::In ? 1.0 / (kInSec * sampleRate_) : 0.0;
        for (int i = 0; i < n; ++i) {
            switchX_ = std::clamp(switchX_ + step, 0.0, 1.0);
            const float g = float(switchX_ * switchX_ * (3 - 2 * switchX_));
            left[i] *= g;
            right[i] *= g;
        }
        switchQuiet_ += n;
        if (switch_ == Switch::Out && switchX_ <= 0) {
            switch_ = Switch::Hold;
            switchHeld_ = 0;
            for (int i = 0; i < kParamCount; ++i)
                if (pendingMask_[size_t(i)]) SetParam(i, pending_[i]);
            pendingMask_.reset();
        } else if (switch_ == Switch::Hold) {
            switchHeld_ += n;
            if ((switchQuiet_ >= kQuietSec * sampleRate_ && switchHeld_ >= kQuietSec * sampleRate_) ||
                switchHeld_ >= kMaxHoldSec * sampleRate_) {
                for (Voice &v : voices_) v.ClearFilters();
                cutoff_ = cutoffTarget_ = values_[kFilterFreq]; // no knob glide across a switch
                switch_ = Switch::In;
            }
        } else if (switch_ == Switch::In && switchX_ >= 1) {
            switch_ = Switch::None;
        }
    }

    Voice &MonoVoice(int slot) {
        lastVoice_ = slot;
        return voices_[size_t(slot)];
    }

    // keepSameTime (poly notes): voices started at the same time (same
    // 16-sample sub-block, the original's event grid) can't be stolen;
    // nullptr when nothing else is left.
    Voice *AllocateVoice(bool keepSameTime = false) {
        // Round robin like Synth1 (its voice squares): the next idle voice
        // after the last one started. (measured, measure_steal.py) at the
        // limit a RELEASED voice goes first (oldest of those), only then the
        // oldest held one.
        const int limit = std::clamp<int>(values_[kPolyphony], 1, kMaxVoices);
        Voice *oldest = nullptr, *released = nullptr, *pick = nullptr;
        for (int k = 1; k <= limit && !pick; ++k) {
            Voice &v = voices_[(lastVoice_ + k) % limit];
            if (!v.Active()) pick = &v;
            else if (keepSameTime && v.Age() / 16 == now_ / 16) continue; // the original's 16-sample event grid
            else if (Voice *&steal = v.Gate() ? oldest : released; !steal || v.Age() < steal->Age()) steal = &v;
        }
        if (!pick) pick = released ? released : oldest;
        if (pick) lastVoice_ = int(pick - voices_.data());
        return pick;
    }

    static constexpr int kLfoBlock = Voice::kMaxChunk;
    PatchValues base_{};
    PatchValues values_{};
    int routed_[2] = {-1, -1};
    std::array<int, 128> cc_{};
    int pressure_ = 0;
    bool sustain_ = false;
    std::array<bool, 128> keyDown_{}, sustained_{};
    std::array<uint8_t, 128> keyVelocity_{};
    std::array<Voice, kMaxVoices> voices_{};
    int lastVoice_ = -1; // round robin
    std::array<std::pair<uint8_t, uint8_t>, 32> noteOns_{}; // poly note ons of this sub-block (key, velocity)
    int noteOnCount_ = 0;
    double cutoff_ = kParams[kFilterFreq].def, cutoffTarget_ = kParams[kFilterFreq].def, cutoffRate_ = 0; // knob glide
    double volume_ = 1.0, volumeTarget_ = 1.0; // VolumeGain, ramped per Render
    Lfo lfo_[2];
    TempoDelay delay_;
    Chorus chorus_;
    static constexpr int kEchoChunk = 256;
    float echoL_[kEchoChunk] = {}, echoR_[kEchoChunk] = {};
    Equalizer eq_;
    EffectUnit effect_;
    static constexpr int kMaxRender = 4096; // longer Render calls run in pieces
    float panGain_[2][kMaxRender] = {};
    double lfoBuf_[2][kLfoBlock] = {};
    int32_t lfoSteps_[2][kLfoBlock] = {};
    Rng rng_;
    double sampleRate_ = 44100.0;
    double tempo_ = 120.0;
    double ppq_ = 0.0;
    bool playing_ = false;
    int bend_ = 0;
    int held_[32] = {};
    int heldCount_ = 0;
    int arpKeys_[32] = {};
    int arpCount_ = 0, arpStep_ = -1, arpDir_ = 1, arpVelocity_ = 100, arpPlaying_ = -1;
    int arpCountdown_ = 0, arpGateCountdown_ = 0;
    double arpAccum_ = 0;
    uint64_t now_ = 0; // samples rendered: a voice's age = its start time
    int64_t blockPos_ = 0; // samples since BeginBlock
    int64_t blockEnd_ = INT64_MAX; // the host block's length (none: tools rendering without BeginBlock)
    enum class Switch { None, Out, Hold, In } switch_ = Switch::None; // patch switch fade
    double switchX_ = 1;
    int64_t switchQuiet_ = 0, switchHeld_ = 0;
    PatchValues pending_{};
    std::bitset<kParamCount> pendingMask_;
};

} // namespace oxo::s2
