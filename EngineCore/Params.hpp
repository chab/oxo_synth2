#pragma once

// The 99 Synth1 parameters, in Synth1's own index order (= .sy1 line index =
// the original AU's parameter ID = our VST3 parameter ID, so automation and
// patches line up). Values are NATIVE integers, exactly what a .sy1 file and
// the original's state chunk store (ranges measured: reference/meta/
// nativemap.txt). Defaults = the original's built-in init patch.

#include "MeasuredTables.hpp"

#include <array>
#include <cstdint>

namespace oxo::s2 {

enum ParamId : int {
    kOsc1Shape, kOsc2Shape, kOsc2Pitch, kOsc2Fine, kOsc2KbdTrack, kOscMix, kOsc2Sync, kOsc2Ring,
    kOscPulseWidth, kOscKeyShift, kModEnvOn, kModEnvAmount, kModEnvAttack, kModEnvDecay,
    kFilterType, kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease, kFilterFreq,
    kFilterRes, kFilterAmount, kFilterKbdTrack, kFilterSat, kFilterVelSwitch,
    kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease, kAmpGain, kAmpVelSens,
    kArpType, kArpOctRange, kArpBeat, kArpGate, kDelayTime, kDelayFeedback, kDelayDryWet,
    kPlayMode, kPortaTime, kBendRange, kLfo1Dest, kLfo1Type, kLfo1Speed, kLfo1Depth, kOsc1FM,
    kLfo2Dest, kLfo2Type, kLfo2Speed, kLfo2Depth, kWheelSpeedSens, kWheelAmountSens,
    kChorusTime, kChorusDepth, kChorusRate, kChorusFeedback, kChorusLevel,
    kLfo1On, kLfo2On, kArpOn, kEqTone, kEqFreq, kEqLevel, kEqQ, kChorusType, kDelayOn, kChorusOn,
    kLfo1TempoSync, kLfo1KeySync, kLfo2TempoSync, kLfo2KeySync, kModEnvDest, kOscFineTune,
    kUnisonOn, kPortaAuto, kUnisonDetune, kOsc1Detune, kEffectOn, kEffectType, kEffectCtrl1,
    kEffectCtrl2, kEffectLevel, kDelayType, kDelaySpread, kUnisonPan, kUnisonPitch,
    kCtrlSrc1, kCtrlAssign1, kCtrlSrc2, kCtrlAssign2, kPan, kOscPhase, kUnisonPhase,
    kUnisonVoices, kPolyphony, kSubGain, kSubShape, kSubOctave, kDelayTone,
    kParamCount
};

struct ParamInfo {
    const char *key;   // stable snake_case id (UI, docs)
    const char *name;  // the original's published name
    int32_t min, max, def;
    bool automatable;
};

// clang-format off
inline constexpr std::array<ParamInfo, kParamCount> kParams = {{
    {"osc1_shape", "osc1 shape", 0, 3, 2, true},
    {"osc2_shape", "osc2 shape", 1, 4, 1, true},
    {"osc2_pitch", "osc2 pitch", 0, 127, 51, true},
    {"osc2_fine", "osc2 fine tune", 0, 127, 81, true},
    {"osc2_kbd_track", "osc2 kbd track", 0, 1, 1, true},
    {"osc_mix", "osc mix", 0, 127, 69, true},
    {"osc2_sync", "osc2 sync", 0, 1, 0, true},
    {"osc2_ring", "osc2 ring modulation", 0, 1, 0, true},
    {"osc_pulse_width", "osc pulse width", 0, 127, 64, true},
    {"osc_key_shift", "osc key shift", -24, 24, 0, true},
    {"mod_env_on", "osc mod env on/off", 0, 1, 0, true},
    {"mod_env_amount", "osc mod env amount", 0, 127, 64, true},
    {"mod_env_attack", "osc mod env attack", 0, 127, 0, true},
    {"mod_env_decay", "osc mod env decay", 0, 127, 0, true},
    {"filter_type", "filter type", 0, 4, 1, true},
    {"filter_attack", "filter attack", 0, 127, 0, true},
    {"filter_decay", "filter decay", 0, 127, 40, true},
    {"filter_sustain", "filter sustain", 0, 127, 21, true},
    {"filter_release", "filter release", 0, 127, 69, true},
    {"filter_freq", "filter freq", 0, 127, 32, true},
    {"filter_res", "filter resonance", 0, 127, 22, true},
    {"filter_amount", "filter amount", 0, 127, 83, true},
    {"filter_kbd_track", "filter kbd track", 0, 127, 64, true},
    {"filter_sat", "filter saturation", 0, 127, 0, true},
    {"filter_vel_switch", "filter velocity switch", 0, 1, 0, true},
    {"amp_attack", "amp attack", 0, 127, 0, true},
    {"amp_decay", "amp decay", 0, 127, 64, true},
    {"amp_sustain", "amp sustain", 0, 127, 107, true},
    {"amp_release", "amp release", 0, 127, 78, true},
    {"amp_gain", "amp gain", 0, 127, 68, true},
    {"amp_vel_sens", "amp velocity sens", 0, 127, 0, true},
    {"arp_type", "arpeggiator type", 1, 4, 1, true},
    {"arp_oct_range", "arpeggiator oct range", 0, 3, 0, true},
    {"arp_beat", "arpeggiator beat", 0, 18, 11, true},
    {"arp_gate", "arpeggiator gate", 0, 127, 64, true},
    {"delay_time", "delay time", 0, 19, 8, true},
    {"delay_feedback", "delay feedback", 0, 127, 51, true},
    {"delay_dry_wet", "delay dry/wet", 0, 127, 20, true},
    {"play_mode", "play mode type", 0, 2, 0, true},
    {"porta_time", "portament time", 0, 127, 0, true},
    {"bend_range", "pitch bend range", 0, 24, 12, true},
    {"lfo1_dest", "lfo1 destination", 1, 7, 3, true},
    {"lfo1_type", "lfo1 type", 0, 5, 5, true},
    {"lfo1_speed", "lfo1 speed", 0, 127, 48, true},
    {"lfo1_depth", "lfo1 depth", 0, 127, 19, true},
    {"osc1_fm", "osc1 FM", 0, 127, 0, true},
    {"lfo2_dest", "lfo2 destination", 1, 7, 5, true},
    {"lfo2_type", "lfo2 type", 0, 5, 1, true},
    {"lfo2_speed", "lfo2 speed", 0, 127, 64, true},
    {"lfo2_depth", "lfo2 depth", 0, 127, 64, true},
    {"wheel_speed_sens", "midi ctrl sens1", 0, 127, 74, true},
    {"wheel_amount_sens", "midi ctrl sens2", 0, 127, 74, true},
    {"chorus_time", "chorus delay time", 0, 127, 64, true},
    {"chorus_depth", "chorus depth", 0, 127, 64, true},
    {"chorus_rate", "chorus rate", 0, 127, 50, true},
    {"chorus_feedback", "chorus feedback", 0, 127, 64, true},
    {"chorus_level", "chorus level", 0, 127, 40, true},
    {"lfo1_on", "lfo1 on/off", 0, 1, 1, true},
    {"lfo2_on", "lfo2 on/off", 0, 1, 1, true},
    {"arp_on", "arpeggiator on/off", 0, 1, 0, true},
    {"eq_tone", "equalizer tone", 0, 127, 64, true},
    {"eq_freq", "equalizer freq", 0, 127, 64, true},
    {"eq_level", "equalizer level", 0, 127, 64, true},
    {"eq_q", "equalizer Q", 0, 127, 64, true},
    {"chorus_type", "chorus type", 1, 4, 2, true},
    {"delay_on", "delay on/off", 0, 1, 0, true},
    {"chorus_on", "chorus on/off", 0, 1, 0, true},
    {"lfo1_tempo_sync", "lfo1 tempo sync", 0, 1, 0, true},
    {"lfo1_key_sync", "lfo1 key sync", 0, 1, 0, true},
    {"lfo2_tempo_sync", "lfo2 tempo sync", 0, 1, 0, true},
    {"lfo2_key_sync", "lfo2 key sync", 0, 1, 0, true},
    {"mod_env_dest", "osc mod dest", 0, 2, 0, true},
    {"osc_fine_tune", "osc1,2 fine tune", 0, 127, 64, true},
    {"unison_on", "unison mode", 0, 1, 0, true},
    {"porta_auto", "portament auto mode", 0, 1, 0, true},
    {"unison_detune", "unison detune", 0, 127, 22, true},
    {"osc1_detune", "osc1 detune", 0, 127, 0, true},
    {"effect_on", "effect on/off", 0, 1, 0, true},
    {"effect_type", "effect type", 0, 9, 0, true},
    {"effect_ctrl1", "effect control1", 0, 127, 64, true},
    {"effect_ctrl2", "effect control2", 0, 127, 64, true},
    {"effect_level", "effect level/mix", 0, 127, 64, true},
    {"delay_type", "delay type", 0, 2, 0, true},
    {"delay_spread", "delay time spread", 0, 127, 66, true},
    {"unison_pan", "unison pan spread", 0, 127, 64, true},
    {"unison_pitch", "unison pitch", 0, 48, 24, true},
    {"ctrl_src1", "midi ctrl src1", -1, 65535, 45057, false},
    {"ctrl_assign1", "midi ctrl assign1", -1, 65535, 44, false},
    {"ctrl_src2", "midi ctrl src2", -1, 65535, 45057, false},
    {"ctrl_assign2", "midi ctrl assign2", -1, 65535, 43, false},
    {"pan", "pan", 0, 127, 64, true},
    {"osc_phase", "osc phase shift", 0, 127, 0, true},
    {"unison_phase", "unison phase shift", 0, 127, 0, true},
    {"unison_voices", "unison voice num", 2, 8, 2, true},
    {"polyphony", "polyphony", 1, 32, 16, false},
    {"sub_gain", "osc1 sub gain", 0, 127, 0, true},
    {"sub_shape", "osc1 sub shape", 0, 3, 1, true},
    {"sub_octave", "osc1 sub octave", 0, 1, 1, true},
    {"delay_tone", "delay tone", 0, 127, 64, true},
}};
// clang-format on

using PatchValues = std::array<int32_t, kParamCount>;

// Master volume (Synth1's "vol" knob, left of the LCD): a plugin parameter
// right after the patch (ID 99) but NOT part of it: never in a .sy1, a patch
// load leaves it alone; the plugin state (DAW session) keeps it. (measured,
// measure_volume.py: Synth1 keeps it in chunk word 651) 0..127, 64 = unity
// (default), 32 = -6.9 dB, 127 = +10.9 dB, 0 = silence: tables::kVolumeGain.
inline constexpr int kVolumeParamId = kParamCount;
inline constexpr ParamInfo kVolumeParam = {"volume", "volume", 0, 127, 64, true};
inline constexpr double VolumeGain(int32_t v) { return tables::kVolumeGain[v < 0 ? 0 : v > 127 ? 127 : v]; }

// Info of a patch parameter or of the volume (out-of-range ids clamp).
inline constexpr const ParamInfo &Info(int id) {
    return id == kVolumeParamId ? kVolumeParam : kParams[id < 0 ? 0 : id >= kParamCount ? kParamCount - 1 : id];
}

// Arp beat (param 33): step length in beats (quarter notes).
inline constexpr double kArpBeats[19] = {4.0,  3.5,     3.0, 2.0,     1.75, 1.5,     4.0 / 3, 1.0,   0.875,   0.75,
                                         2.0 / 3, 0.5, 0.375, 1.0 / 3, 0.25, 1.0 / 6, 0.125,   1.0 / 12, 1.0 / 24};

inline constexpr PatchValues DefaultValues() {
    PatchValues v{};
    for (int i = 0; i < kParamCount; ++i) v[i] = kParams[i].def;
    return v;
}

inline constexpr int32_t ClampValue(int id, int64_t v) {
    const ParamInfo &p = Info(id);
    return static_cast<int32_t>(v < p.min ? p.min : v > p.max ? p.max : v);
}

inline constexpr double ToNormalized(int id, int32_t v) {
    const ParamInfo &p = Info(id);
    return p.max == p.min ? 0.0 : double(ClampValue(id, v) - p.min) / double(p.max - p.min);
}

// Values the editors never offer (pick lists, wheel, drag): chorus type 3,
// a duplicate of 2 (the engine plays it as 2) that Synth1's panel never
// shows (its x1 / x2 / x4 = 1 / 2 / 4). Patches and automation may still
// carry it. Swift mirror: SynthModel.offered / stepped.
inline constexpr bool Offered(int id, int32_t v) { return !(id == kChorusType && v == 3); }

// `steps` offered values away from v, clamped to the range; an unoffered v
// counts as the offered value below it (steps 0 = that value).
inline constexpr int32_t OfferedStep(int id, int32_t v, int steps) {
    const ParamInfo &p = Info(id);
    int32_t x = ClampValue(id, v);
    while (x > p.min && !Offered(id, x)) --x;
    const int dir = steps > 0 ? 1 : -1;
    for (int n = steps < 0 ? -steps : steps; n > 0; --n) {
        int32_t y = x + dir;
        while (y >= p.min && y <= p.max && !Offered(id, y)) y += dir;
        if (y < p.min || y > p.max) break;
        x = y;
    }
    return x;
}

// VST3 discrete-parameter convention (RangeParameter::toPlain with stepCount
// = max - min), so processor and controller agree on every step.
inline constexpr int32_t FromNormalized(int id, double n) {
    const ParamInfo &p = Info(id);
    const int64_t range = p.max - p.min;
    const double x = n < 0 ? 0 : n > 1 ? 1 : n;
    const int64_t step = static_cast<int64_t>(x * double(range + 1));
    return ClampValue(id, p.min + (step > range ? range : step));
}

} // namespace oxo::s2
