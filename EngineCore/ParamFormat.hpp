#pragma once

// Display text for native parameter values (GUI readouts, choice labels).
// Portable: shared by every editor (mac SwiftUI via the bridge, later iPad /
// Windows).

#include "Effects.hpp"
#include "Lfo.hpp"
#include "MeasuredTables.hpp"
#include "Params.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace oxo::s2 {

inline std::string Signed(int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); }

// Beats -> note value: 0.5 -> "1/8", 1/12 -> "1/32T", 0.75 -> "1/8.",
// 0.875 -> "1/8..", 4 -> "1 bar", 6 -> "1/1.", 8 -> "2 bars".
inline std::string DivisionLabel(double beats) {
    if (beats >= 8.0 - 1e-6 || std::fabs(beats - 4.0) < 1e-6) {
        const double bars = beats / 4.0;
        char b[32];
        std::snprintf(b, sizeof b, std::fabs(bars - std::round(bars)) < 1e-6 ? "%.0f bar%s" : "%.3g bars", bars,
                      std::fabs(bars - 1) < 1e-6 ? "" : "s");
        return b;
    }
    const double whole = beats / 4.0; // fraction of a whole note
    for (int q = 1; q <= 512; q *= 2) {
        if (std::fabs(whole * q - 1) < 1e-4) return "1/" + std::to_string(q);
        if (std::fabs(whole * q * 1.5 - 1) < 1e-4) return "1/" + std::to_string(q) + "T";
        if (std::fabs(whole * q / 1.5 - 1) < 1e-4) return "1/" + std::to_string(q) + ".";
        if (std::fabs(whole * q / 1.75 - 1) < 1e-4) return "1/" + std::to_string(q) + "..";
    }
    for (int q = 1; q <= 512; q *= 2)
        for (int p = 1; p < q; ++p)
            if (std::fabs(whole * q - p) < 1e-4) return std::to_string(p) + "/" + std::to_string(q);
    char b[32];
    std::snprintf(b, sizeof b, "%.3g beat", beats);
    return b;
}

// The tempo sync switch that turns a speed knob into a note division (-1 = none).
inline int TempoSyncSwitch(int id) { return id == kLfo1Speed ? kLfo1TempoSync : id == kLfo2Speed ? kLfo2TempoSync : -1; }

// Readout name: the effect knobs (ctl1 / ctl2 / level) are named after what
// they do in the current effect type (Synth1 manual v1.12 table, matches
// EffectUnit); every other parameter = its Info name.
inline const char *DisplayName(int id, int effectType) {
    static constexpr const char *kEffectKnobs[10][3] = {
        {"dist amount", "LPF cutoff", "level"}, {"dist amount", "LPF cutoff", "level"},
        {"dist amount", "LPF cutoff", "level"}, {"sample rate", "bit depth", "mix"},
        {"mod freq", "unused", "mix"},          {"comp depth", "attack time", "level"},
        {"LFO depth", "LFO freq", "feedback"},  {"LFO depth", "LFO freq", "feedback"},
        {"LFO depth", "LFO freq", "feedback"},  {"LFO depth", "LFO freq", "feedback"},
    };
    if (id >= kEffectCtrl1 && id <= kEffectLevel && effectType >= 0 && effectType < 10)
        return kEffectKnobs[effectType][id - kEffectCtrl1];
    return Info(id).name;
}

// tempoSync = the value of TempoSyncSwitch(id), when there is one;
// effectType = kEffectType's value (phasers: level = feedback in %, 64 = 0).
inline std::string FormatParam(int id, int v, bool tempoSync = false, int effectType = -1) {
    auto pick = [&](std::initializer_list<const char *> names, int base = 0) -> std::string {
        const int i = v - base;
        return i >= 0 && i < int(names.size()) ? names.begin()[i] : std::to_string(v);
    };
    if (id == kVolumeParamId) {
        if (v <= 0) return "-inf dB";
        char buf[16];
        std::snprintf(buf, sizeof buf, "%+.1f dB", 20.0 * std::log10(VolumeGain(v)));
        return buf;
    }
    switch (id) {
    case kOsc1Shape: return pick({"sine", "saw", "pulse", "triangle"});
    case kOsc2Shape: return pick({"saw", "pulse", "triangle", "noise"}, 1);
    case kSubShape: return pick({"sine", "triangle", "saw", "square"});
    case kSubOctave: return pick({"0 oct", "-1 oct"});
    case kFilterType: return pick({"LP12", "LP24", "HP12", "BP12", "LPDL"});
    case kLfo1Type:
    case kLfo2Type: return pick({"saw", "triangle", "square", "random (S&H)", "random (smooth)", "sine"});
    case kLfo1Dest:
    case kLfo2Dest: return pick({"osc2", "osc1+2", "filter", "amp", "p/w", "FM", "pan"}, 1);
    case kModEnvDest: return pick({"osc2", "FM", "p/w"});
    case kArpType: return pick({"up&down", "up", "down", "random"}, 1);
    case kArpOctRange: return std::to_string(v + 1) + " oct";
    case kArpBeat: return DivisionLabel(kArpBeats[std::clamp(v, 0, 18)]);
    case kDelayTime: return v == 0 ? "min" : DivisionLabel(kDelayBeats[std::clamp(v, 0, 19)]);
    case kDelayType: return pick({"ST", "X", "PP"});
    case kPlayMode: return pick({"poly", "mono", "legato"});
    case kChorusType: return pick({"x1", "x2", "x2", "x4"}, 1); // Synth1's labels; 3 plays as 2
    case kEffectType: return pick({"AD1", "AD2", "DD", "DECI", "RM", "COMP", "PH1", "PH2", "PH3", "PH4"});
    case kEffectLevel:
        if (effectType < 6) return std::to_string(v);
        return Signed(int(std::lround((v - 64) * 100.0 / (v >= 64 ? 63 : 64)))) + "%";
    case kOsc2Pitch: return Signed(tables::kOsc2PitchSemis[v]) + " st";
    case kOsc2Fine:
    case kOscFineTune: return Signed(tables::kFineCents[v]) + " ct";
    case kOscKeyShift: return Signed(v) + " st";
    case kUnisonPitch: return Signed(v - 24) + " st";
    case kBendRange: return std::to_string(v) + " st";
    case kLfo1Speed:
    case kLfo2Speed: {
        if (tempoSync) return DivisionLabel(Lfo::SyncBeats(v));
        char b[32];
        const double hz = Lfo::SpeedHz(v);
        std::snprintf(b, sizeof b, hz < 10 ? "%.2f Hz" : "%.1f Hz", hz);
        return b;
    }
    case kOsc2KbdTrack:
    case kOsc2Sync:
    case kOsc2Ring:
    case kModEnvOn:
    case kFilterVelSwitch:
    case kLfo1On:
    case kLfo2On:
    case kArpOn:
    case kDelayOn:
    case kChorusOn:
    case kLfo1TempoSync:
    case kLfo1KeySync:
    case kLfo2TempoSync:
    case kLfo2KeySync:
    case kUnisonOn:
    case kPortaAuto:
    case kEffectOn: return v ? "on" : "off";
    case kModEnvAmount:
    case kFilterAmount:
    case kWheelSpeedSens:
    case kWheelAmountSens:
    case kChorusFeedback:
    case kEqTone:
    case kEqLevel:
    case kDelaySpread:
    case kUnisonPan:
    case kPan:
    case kDelayTone: return Signed(v - 64);
    default: return std::to_string(v);
    }
}

} // namespace oxo::s2
