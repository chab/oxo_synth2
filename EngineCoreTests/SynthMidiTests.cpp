#include "TestFramework.hpp"

#include "../EngineCore/Synth.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace oxo::s2;

namespace {
Synth RoutedToCutoff(int base, int sens) {
    Synth s;
    s.SetParam(kFilterFreq, base);
    s.SetParam(kCtrlSrc1, 0xB001);
    s.SetParam(kCtrlAssign1, kFilterFreq);
    s.SetParam(kWheelSpeedSens, sens);
    s.SetParam(kCtrlSrc2, 0xB002);
    s.SetParam(kCtrlAssign2, kFilterFreq);
    s.SetParam(kWheelAmountSens, 64);
    return s;
}

void Run(Synth &s, int frames) {
    std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
    s.Render(l.data(), r.data(), frames);
}
} // namespace

// Points of reference/meta/wheel.json (measured on the original).
OXO_TEST(CtrlRoutingMatchesMeasuredLaw) {
    struct P { int base, sens, cc, offset; };
    const P points[] = {{64, 65, 32, 1},  {64, 65, 16, 0},   {64, 66, 16, 1},   {64, 80, 127, 33},
                        {64, 96, 64, 33}, {64, 112, 48, 37}, {64, 63, 127, 0},  {64, 62, 32, -1},
                        {64, 60, 127, -6}, {64, 48, 64, -15}, {64, 16, 63, -47}, {64, 0, 127, -64},
                        {40, 127, 64, 65}, {40, 32, 63, -31}, {40, 1, 8, -8}};
    for (const P &p : points) {
        Synth s = RoutedToCutoff(p.base, p.sens);
        s.ControlChange(1, p.cc);
        OXO_EXPECT_EQ(s.EffectiveParam(kFilterFreq) - p.base, p.offset);
        OXO_EXPECT_EQ(s.Param(kFilterFreq), p.base); // the patch itself never moves
    }
}

OXO_TEST(CtrlRoutingFollowsPatchEdits) {
    Synth s = RoutedToCutoff(64, 80);
    s.ControlChange(1, 127);
    s.SetParam(kFilterFreq, 50);
    OXO_EXPECT_EQ(s.EffectiveParam(kFilterFreq), 50 + 33);
    s.SetParam(kCtrlAssign1, kFilterRes); // re-routing restores the old target
    OXO_EXPECT_EQ(s.EffectiveParam(kFilterFreq), 50);
    OXO_EXPECT_EQ(s.EffectiveParam(kFilterRes), s.Param(kFilterRes) + 33 > 127 ? 127 : s.Param(kFilterRes) + 33);
    s.ControlChange(1, 0);
    OXO_EXPECT_EQ(s.EffectiveParam(kFilterRes), s.Param(kFilterRes));
}

OXO_TEST(DefaultPatchWheelDrivesLfo1) {
    Synth s; // init patch: CC1 -> lfo1 depth / speed, sens 74
    const int depth = s.Param(kLfo1Depth), speed = s.Param(kLfo1Speed);
    s.ControlChange(1, 127);
    OXO_EXPECT_EQ(s.EffectiveParam(kLfo1Depth), depth + 20);
    OXO_EXPECT_EQ(s.EffectiveParam(kLfo1Speed), speed + 20);
}

OXO_TEST(SustainPedalHoldsReleasedKeys) {
    Synth s;
    s.NoteOn(60, 100);
    s.ControlChange(64, 127);
    s.NoteOff(60);
    s.NoteOn(64, 100);
    s.NoteOff(64);
    Run(s, 256);
    OXO_EXPECT_EQ(s.GatedVoices(), 2);
    s.NoteOn(67, 100); // still held when the pedal comes up: keeps sounding
    s.ControlChange(64, 0);
    OXO_EXPECT_EQ(s.GatedVoices(), 1);
    s.NoteOff(67);
    OXO_EXPECT_EQ(s.GatedVoices(), 0);
}

OXO_TEST(SustainPedalReleasesOnAllNotesOff) {
    Synth s;
    s.ControlChange(64, 127);
    s.NoteOn(60, 100);
    s.NoteOff(60);
    s.ControlChange(123, 0);
    s.ControlChange(64, 0);
    OXO_EXPECT_EQ(s.GatedVoices(), 0);
}

// Synth1's voice squares: each note takes the NEXT idle voice, not voice 1 again.
OXO_TEST(VoicesRoundRobin) {
    Synth s;
    s.SetSampleRate(48000);
    s.SetParam(kAmpRelease, 0);
    const auto voiceOf = [&](uint64_t states) {
        for (int i = 0; i < kMaxVoices; ++i)
            if ((states >> (2 * i) & 3) == Synth::kVoiceHeld) return i;
        return -1;
    };
    for (int n = 0; n < 3; ++n) {
        s.NoteOn(60, 100);
        Run(s, 64);
        OXO_EXPECT_EQ(voiceOf(s.VoiceStates()), n);
        s.NoteOff(60);
        Run(s, 4800); // release over: the voice is idle again
        OXO_EXPECT_EQ(s.VoiceStates(), uint64_t(0));
    }
}

// (measured) with osc phase 0 the osc1 detune copies of a unison stack start
// at unrelated phases: 8 voices add ~8x the power of one (coherent copies:
// ~58x, the old bug).
OXO_TEST(UnisonDetuneCopiesAreIncoherent) {
    const auto power = [](bool unison) {
        Synth s;
        s.SetSampleRate(48000);
        s.SetParam(kOsc1Shape, 0);
        s.SetParam(kOsc1Detune, 127);
        s.SetParam(kAmpRelease, 0);
        s.SetParam(kUnisonOn, unison);
        s.SetParam(kUnisonVoices, 8);
        s.SetParam(kUnisonDetune, 0);
        s.SetParam(kUnisonPan, 64);
        std::vector<float> l(48000), r(48000);
        double sum = 0;
        for (int n = 0; n < 8; ++n) {
            s.NoteOn(69, 127);
            s.Render(l.data(), r.data(), 48000);
            for (float x : l) sum += double(x) * x;
            s.NoteOff(69);
            Run(s, 4800);
        }
        return sum;
    };
    const double ratio = power(true) / power(false);
    OXO_EXPECT(ratio > 4 && ratio < 20);
}

// (measured) unison pan spread below 64 sends the LOWEST voice right, and
// the patch pan multiplies its gains with the spread's (hard left patch pan:
// the right-spread voice falls silent instead of moving to the centre).
OXO_TEST(UnisonPanSpreadDirectionAndPatchPan) {
    const auto render = [](int patchPan, std::vector<float> &l, std::vector<float> &r) {
        Synth s;
        s.SetSampleRate(48000);
        s.SetParam(kOsc1Shape, 0);
        s.SetParam(kOscMix, 0);
        s.SetParam(kUnisonOn, 1);
        s.SetParam(kUnisonVoices, 2);
        s.SetParam(kUnisonDetune, 127); // +-50 cents
        s.SetParam(kUnisonPan, 0);
        s.SetParam(kPan, patchPan);
        l.assign(48000, 0), r.assign(48000, 0);
        s.NoteOn(69, 127);
        s.Render(l.data(), r.data(), 48000);
    };
    const auto crossings = [](const std::vector<float> &x) {
        int n = 0;
        for (size_t i = 1; i < x.size(); ++i) n += (x[i - 1] < 0) != (x[i] < 0);
        return n;
    };
    std::vector<float> l, r;
    render(64, l, r);
    OXO_EXPECT(crossings(l) > crossings(r) + 20); // left = the voice 50 cents up
    render(0, l, r);
    double right = 0;
    for (float x : r) right += double(x) * x;
    OXO_EXPECT(right < 1e-9);
}

// (measured) the patch pan and an LFO on pan act AFTER the effect: a d.d.
// folds the centred signal once, the two channels are that output times the
// pan gains (panned before the fold, a 3 % level difference decorrelated them).
OXO_TEST(PanActsAfterTheEffect) {
    for (const bool lfo : {false, true}) {
        Synth s;
        s.SetSampleRate(44100);
        s.SetParam(kOsc1Shape, 0);
        s.SetParam(kOscMix, 0);
        s.SetParam(kEffectOn, 1);
        s.SetParam(kEffectType, 2);
        s.SetParam(kEffectCtrl1, 64);
        s.SetParam(kPan, lfo ? 64 : 62);
        if (lfo) {
            s.SetParam(kLfo1On, 1);
            s.SetParam(kLfo1Dest, 7);
            s.SetParam(kLfo1Type, 5);
            s.SetParam(kLfo1Speed, 70);
            s.SetParam(kLfo1Depth, 100);
        }
        std::vector<float> l(22050), r(22050);
        s.NoteOn(45, 127);
        s.Render(l.data(), r.data(), 22050);
        // per 64-sample window: r = g l, the residue against r's power
        double res = 0, total = 0;
        for (size_t a = 4096; a + 64 <= l.size(); a += 64) {
            double lr = 0, ll = 0;
            for (size_t i = a; i < a + 64; ++i) lr += double(l[i]) * r[i], ll += double(l[i]) * l[i];
            const double g = ll > 0 ? lr / ll : 0;
            for (size_t i = a; i < a + 64; ++i) res += (r[i] - g * l[i]) * (r[i] - g * l[i]), total += double(r[i]) * r[i];
        }
        OXO_EXPECT(total > 0 && res < 1e-3 * total);
    }
}

// (measured) delay type 1 (X): a left input's first echo comes out on the
// RIGHT, the second on the left.
OXO_TEST(CrossDelayEchoesLeftInputRightFirst) {
    Synth s;
    s.SetSampleRate(44100);
    s.SetTransport(120, 0, true);
    s.SetParam(kPan, 0);
    s.SetParam(kAmpRelease, 0);
    s.SetParam(kDelayOn, 1);
    s.SetParam(kDelayType, 1);
    s.SetParam(kDelayTime, 8); // half a beat: 11025 samples
    s.SetParam(kDelayFeedback, 80);
    s.SetParam(kDelayDryWet, 64);
    std::vector<float> l(30000), r(30000);
    s.NoteOn(60, 127);
    s.Render(l.data(), r.data(), 1024);
    s.NoteOff(60);
    s.Render(l.data() + 1024, r.data() + 1024, 30000 - 1024);
    const auto energy = [](const std::vector<float> &x, size_t a, size_t b) {
        double e = 0;
        for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i];
        return e;
    };
    OXO_EXPECT(energy(r, 11025, 13000) > 100 * energy(l, 11025, 13000));
    OXO_EXPECT(energy(l, 22050, 24000) > 100 * energy(r, 22050, 24000));
}

// (measured, unison spread into the comp.) the compressor's detector is per
// channel: a loud left channel does not turn the right one down.
OXO_TEST(CompressorDetectsEachChannel) {
    const auto rightOut = [](float leftLevel) {
        PatchValues p = DefaultValues();
        p[kEffectType] = 5;
        EffectUnit fx;
        fx.Prepare(44100);
        std::vector<float> l(4410), r(4410);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = leftLevel * float(std::sin(0.05 * double(i))), r[i] = 0.1f * float(std::sin(0.07 * double(i)));
        fx.Process(l.data(), r.data(), int(l.size()), p);
        return r;
    };
    OXO_EXPECT(rightOut(0.0f) == rightOut(1.0f));
}

// (measured, small noise, 1..4 kHz) the a.d.1 small-signal gain per ctl1 is
// not monotonic: lowest near 16 (the drive rises 70 dB over ctl1, the output
// gain falls from x8 to x1 up to ~48: measure_ad1_law.py).
OXO_TEST(Ad1SmallSignalGainFollowsTheOriginal) {
    const auto gain = [](int c1) {
        PatchValues p = DefaultValues();
        p[kEffectType] = 0, p[kEffectCtrl1] = c1, p[kEffectCtrl2] = 127, p[kEffectLevel] = 127;
        EffectUnit fx;
        fx.Prepare(44100);
        std::vector<float> l(44100), r(44100);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 1e-5f * float(std::sin(2 * M_PI * 2500.0 * double(i) / 44100));
        fx.Process(l.data(), r.data(), int(l.size()), p);
        double peak = 0;
        for (size_t i = 22050; i < l.size(); ++i) peak = std::max(peak, std::fabs(double(l[i])));
        return peak / 1e-5;
    };
    const std::pair<int, double> measured[] = {{0, 2.401}, {16, 1.259}, {32, 2.362}, {64, 17.47}, {96, 132.8}, {127, 947.0}};
    for (const auto &[c1, g] : measured) OXO_EXPECT(std::fabs(20 * std::log10(gain(c1) / g)) < 1.0);
}

// (measured) a unison chord over the polyphony keeps its FIRST notes: a stack
// never steals from a note started at the same time.
OXO_TEST(UnisonChordKeepsFirstNotes) {
    Synth s;
    s.SetSampleRate(48000);
    s.SetParam(kUnisonOn, 1);
    s.SetParam(kUnisonVoices, 8);
    s.SetParam(kPolyphony, 16);
    s.SetParam(kAmpRelease, 0);
    const auto held = [&] {
        int n = 0;
        for (uint64_t st = s.VoiceStates(); st; st >>= 2) n += (st & 3) == Synth::kVoiceHeld;
        return n;
    };
    for (int key : {60, 64, 67}) s.NoteOn(key, 100);
    Run(s, 64);
    OXO_EXPECT_EQ(held(), 16);
    s.NoteOff(67); // dropped: nothing to release
    Run(s, 64);
    OXO_EXPECT_EQ(held(), 16);
    s.NoteOff(60);
    Run(s, 64);
    OXO_EXPECT_EQ(held(), 8);
}

// Stealing a sounding voice must not click: its filter / output state and a
// free-running sub keep going (the new note's envelope starts from there).
OXO_TEST(StealingASoundingVoiceDoesNotClick) {
    Synth s;
    s.SetSampleRate(48000);
    s.SetParam(kPolyphony, 1);
    s.SetParam(kSubGain, 127);
    s.SetParam(kSubOctave, 1);
    s.SetParam(kOscMix, 0);
    s.SetParam(kOsc1Shape, 0);
    s.SetParam(kAmpAttack, 40);
    s.SetParam(kAmpSustain, 127);
    s.SetParam(kLfo1On, 0);
    s.SetParam(kLfo2On, 0);
    s.SetParam(kFilterFreq, 127);
    s.SetParam(kFilterAmount, 64);
    std::vector<float> x(48002), r(48002);
    s.NoteOn(48, 100);
    s.Render(x.data(), r.data(), 24002);
    s.NoteOn(50, 100); // steals the only voice (the same key would release it first)
    s.Render(x.data() + 24002, r.data(), 24000);
    // a click = a jump off the waveform's curve: large second difference
    const auto kink = [&](int from, int to) {
        float k = 0;
        for (int i = from; i < to; ++i) k = std::max(k, std::fabs(x[size_t(i)] - 2 * x[size_t(i - 1)] + x[size_t(i - 2)]));
        return k;
    };
    OXO_EXPECT(kink(24002, 24002 + 256) < 2 * kink(12000, 24000));
}

// A patch switch fades the voices out, lands the new values at silence and
// fades back in; held notes keep playing.
OXO_TEST(PatchSwitchFadesWithoutCuttingNotes) {
    Synth s;
    s.SetSampleRate(48000);
    s.SetParam(kAmpSustain, 127);
    s.NoteOn(60, 100);
    Run(s, 4800);
    PatchValues next = DefaultValues();
    next[kFilterRes] = 120;
    next[kFilterType] = 3;
    next[kAmpSustain] = 127;
    s.BeginPatchChange();
    s.SetPatch(next);
    OXO_EXPECT(s.Param(kFilterRes) != 120); // deferred until silent
    std::vector<float> l(240), r(240);
    float quietest = 1;
    for (int b = 0; b < 200 && s.PatchChanging(); ++b) { // 5 ms blocks
        s.Render(l.data(), r.data(), 240);
        float e = 0;
        for (float x : l) e = std::max(e, std::fabs(x));
        quietest = std::min(quietest, e);
    }
    OXO_EXPECT(!s.PatchChanging());
    OXO_EXPECT(quietest < 1e-6f);
    OXO_EXPECT_EQ(s.Param(kFilterRes), 120);
    OXO_EXPECT_EQ(s.GatedVoices(), 1);
    s.Render(l.data(), r.data(), 240);
    float e = 0;
    for (float x : l) e = std::max(e, std::fabs(x));
    OXO_EXPECT(e > 0.01f);
}

namespace {
// Frequency (Hz) of a sine render over [from, to) samples: interpolated
// upward zero crossings.
double SineHz(const std::vector<float> &x, int from, int to, double sr) {
    double first = -1, last = -1;
    int n = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[size_t(i - 1)] < 0 && x[size_t(i)] >= 0) {
            const double t = i - 1 + x[size_t(i - 1)] / (x[size_t(i - 1)] - x[size_t(i)]);
            if (first < 0) first = t;
            last = t;
            ++n;
        }
    return n > 1 ? (n - 1) * sr / (last - first) : 0;
}

Synth PortaSynth(int mode) {
    Synth s;
    s.SetSampleRate(48000);
    for (const auto &[id, v] : {std::pair{kOsc1Shape, 0}, {kOscMix, 0}, {kPortaTime, 64}, {kPlayMode, mode},
                                 {kFilterFreq, 127}, {kFilterAmount, 64}, {kAmpSustain, 127}, {kLfo1On, 0},
                                 {kLfo2On, 0}, {kChorusOn, 0}, {kDelayOn, 0}, {kAmpRelease, 0}})
        s.SetParam(id, v);
    return s;
}
} // namespace

// (measured, measure_porta.py) a glide lands EXACTLY on the note in finite
// time (porta 64, an octave: ~308 ms) and a never-used voice glides up from
// note 0 (the first note after loading).
OXO_TEST(PortamentoLandsAndStartsFromNoteZero) {
    Synth s = PortaSynth(2);
    std::vector<float> x(48000), r(48000);
    s.NoteOn(60, 127);
    s.Render(x.data(), r.data(), 1440); // 30 ms
    OXO_EXPECT(SineHz(x, 480, 1440, 48000) < 100); // still far below middle C
    s.Render(x.data(), r.data(), 48000);            // settled
    s.NoteOn(72, 127);                              // legato glide up an octave
    s.Render(x.data(), r.data(), 24000);
    const double target = 440 * std::exp2((72 - 69) / 12.0);
    OXO_EXPECT(SineHz(x, 9600, 10800, 48000) < target * std::exp2(-20 / 1200.0)); // 200..225 ms: still gliding
    OXO_EXPECT_NEAR(SineHz(x, 16800, 24000, 48000), target, 0.05);                // 350 ms on: landed
}

// (measured) in poly a voice glides from ITS OWN last pitch, not the last note.
OXO_TEST(PolyPortamentoGlidesFromTheVoicesLastPitch) {
    Synth s = PortaSynth(0);
    s.SetParam(kPolyphony, 2);
    std::vector<float> x(48000), r(48000);
    s.NoteOn(60, 127); // slot 0
    s.Render(x.data(), r.data(), 48000);
    s.NoteOff(60);
    s.NoteOn(72, 127); // slot 1 (glides from note 0)
    s.Render(x.data(), r.data(), 48000);
    s.NoteOff(72);
    Run(s, 4800);
    s.NoteOn(64, 127); // slot 0 again: from 60, upward
    s.Render(x.data(), r.data(), 4800);
    const double hz = SineHz(x, 480, 1440, 48000);
    OXO_EXPECT(hz > 440 * std::exp2((60 - 69) / 12.0) && hz < 440 * std::exp2((64 - 69) / 12.0));
}

// (measured) poly: a key pressed again while held releases its voice and
// starts a fresh one (one held voice, not two stacked).
OXO_TEST(HeldKeyPressedAgainKeepsOneVoice) {
    Synth s;
    s.SetSampleRate(48000);
    s.NoteOn(60, 100);
    Run(s, 480);
    s.NoteOn(60, 100);
    Run(s, 480);
    OXO_EXPECT_EQ(s.GatedVoices(), 1);
}

// (measured) a mono retrigger restarts the oscillator phases (osc phase > 0).
OXO_TEST(MonoRetriggerRestartsPhases) {
    const auto setup = [](Synth &s) {
        s.SetSampleRate(48000);
        s.SetParam(kPlayMode, 1);
        s.SetParam(kOscPhase, 1);
        s.SetParam(kOscMix, 0);
        // flat: no envelope, LFO or chorus state to tell the two apart
        for (const auto &[id, v] : {std::pair{kAmpSustain, 127}, {kFilterFreq, 127}, {kFilterAmount, 64},
                                     {kFilterRes, 0}, {kLfo1On, 0}, {kLfo2On, 0}, {kChorusOn, 0}, {kDelayOn, 0}})
            s.SetParam(id, v);
        Run(s, 4800); // cutoff knob glide over
    };
    Synth held, fresh;
    setup(held);
    setup(fresh);
    std::vector<float> a(1000), b(1000), r(1000);
    held.NoteOn(60, 127);
    held.Render(a.data(), r.data(), 777);
    held.NoteOn(64, 127);
    held.Render(a.data(), r.data(), 1000);
    fresh.NoteOn(64, 127);
    fresh.Render(b.data(), r.data(), 1000);
    float diff = 0;
    for (size_t i = 64; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
    OXO_EXPECT(diff < 0.01f);
}

// Master volume: Synth1's measured vol law, not a patch parameter.
OXO_TEST(MasterVolumeScalesOutput) {
    const auto peak = [](int volume) {
        Synth s;
        s.SetSampleRate(48000);
        s.SetVolume(volume);
        std::vector<float> l(2048), r(2048);
        s.Render(l.data(), r.data(), 64); // ramp done
        s.NoteOn(60, 127);
        s.Render(l.data(), r.data(), 2048);
        float p = 0;
        for (float x : l) p = std::max(p, std::fabs(x));
        return p;
    };
    const float unity = peak(64);
    OXO_EXPECT(unity > 0.01f);
    OXO_EXPECT_NEAR(peak(32), unity * 0.4512914f, 1e-5); // -6.9 dB (measured)
    OXO_EXPECT_NEAR(peak(127), unity * 3.498213f, 1e-4);  // +10.9 dB
    OXO_EXPECT_NEAR(peak(0), 0.0f, 1e-9);
    OXO_EXPECT_EQ(ClampValue(kVolumeParamId, 200), 127);
}

// (measured) a cutoff change glides linearly over 50 ms while a voice sounds.
OXO_TEST(CutoffKnobGlides) {
    const auto render = [](bool jump) {
        Synth s;
        s.SetSampleRate(48000);
        s.SetParam(kFilterFreq, 40);
        s.SetParam(kFilterRes, 0);
        s.SetParam(kOscMix, 0); // osc1 alone: no osc1 / osc2 beating in the energy windows
        s.NoteOn(60, 127);
        std::vector<float> l(4800), r(4800), out;
        s.Render(l.data(), r.data(), 4800);
        if (jump) s.SetParam(kFilterFreq, 100);
        for (int b = 0; b < 4; ++b) { // 4 x 25 ms
            s.Render(l.data(), r.data(), 1200);
            float e = 0;
            for (int i = 0; i < 1200; ++i) e += l[size_t(i)] * l[size_t(i)];
            out.push_back(e);
        }
        return out;
    };
    const auto still = render(false), moved = render(true);
    std::vector<float> gain;
    for (size_t b = 0; b < 4; ++b) gain.push_back(moved[b] / still[b]); // brightening vs no jump
    // first 25 ms: brighter, but short of where it ends up
    OXO_EXPECT(gain[0] > 1.1f);
    OXO_EXPECT(gain[0] < gain[3] * 0.85f);
    // after 50 ms the glide is over
    OXO_EXPECT_NEAR(gain[2] / gain[3], 1.0f, 0.05);
}

// Patch changes while keys are held (the browser keeps notes playing): the
// mono key stack must follow every key, whatever the play mode was when it
// went down / up, or a released key comes back as a stuck note.
OXO_TEST(PlayModeChangesWithHeldKeysLeaveNoStuckNote) {
    const auto held = [](Synth &s) {
        Run(s, 64);
        const uint64_t v = s.VoiceStates();
        int n = 0;
        for (int i = 0; i < kMaxVoices; ++i) n += (v >> (2 * i) & 3) == Synth::kVoiceHeld;
        return n;
    };
    const auto mono = [](Synth &s, bool on) {
        s.SetParam(kPlayMode, on ? 1 : 0);
        s.SetParam(kPolyphony, on ? 1 : 8);
    };
    { // key down in mono, up in poly, then mono again
        Synth s;
        s.SetSampleRate(48000);
        mono(s, true);
        s.NoteOn(57, 100);
        mono(s, false);
        s.NoteOff(57);
        mono(s, true);
        s.NoteOn(60, 100);
        s.NoteOff(60);
        OXO_EXPECT_EQ(held(s), 0);
    }
    { // poly chord held, patch -> mono, keys up
        Synth s;
        s.SetSampleRate(48000);
        mono(s, false);
        s.NoteOn(57, 100);
        s.NoteOn(60, 100);
        mono(s, true);
        s.NoteOn(64, 100);
        s.NoteOff(60);
        s.NoteOff(64);
        s.NoteOff(57);
        OXO_EXPECT_EQ(held(s), 0);
    }
    { // all notes off (CC 123) forgets the held keys
        Synth s;
        s.SetSampleRate(48000);
        mono(s, true);
        s.NoteOn(57, 100);
        s.ControlChange(123, 0);
        s.NoteOn(60, 100);
        s.NoteOff(60);
        OXO_EXPECT_EQ(held(s), 0);
    }
}

// Same with the arpeggiator switched by a patch change under a held key.
OXO_TEST(ArpChangesWithHeldKeysLeaveNoStuckNote) {
    const auto held = [](Synth &s) {
        Run(s, 4800);
        const uint64_t v = s.VoiceStates();
        int n = 0;
        for (int i = 0; i < kMaxVoices; ++i) n += (v >> (2 * i) & 3) == Synth::kVoiceHeld;
        return n;
    };
    { // key down without arp, arp on, key up
        Synth s;
        s.SetSampleRate(48000);
        s.SetParam(kArpOn, 0);
        s.NoteOn(57, 100);
        Run(s, 480);
        s.SetParam(kArpOn, 1);
        Run(s, 480);
        s.NoteOff(57);
        OXO_EXPECT_EQ(held(s), 0);
    }
    { // key down with arp, arp off, key up
        Synth s;
        s.SetSampleRate(48000);
        s.SetParam(kArpOn, 1);
        s.NoteOn(57, 100);
        Run(s, 4800);
        s.SetParam(kArpOn, 0);
        Run(s, 480);
        s.NoteOff(57);
        OXO_EXPECT_EQ(held(s), 0);
    }
}

// (measured) key sync: the key starting an arp phrase restarts the LFO, the
// arp's own steps do not. S&H on amp (depth 127, slowest speed): every step of
// a phrase plays at one level, the next phrase at another.
OXO_TEST(ArpStepsDoNotRestartKeySyncedLfo) {
    Synth s;
    s.SetSampleRate(48000);
    for (const auto [id, v] : {std::pair{kLfo1On, 1}, {kLfo1Dest, 4}, {kLfo1Type, 3}, {kLfo1Speed, 0}, {kLfo1Depth, 127},
                               {kLfo1KeySync, 1}, {kLfo1TempoSync, 0}, {kLfo2On, 0}, {kArpOn, 1}, {kArpGate, 80},
                               {kAmpAttack, 0}, {kAmpDecay, 0}, {kAmpSustain, 127}, {kAmpRelease, 0}, {kOsc1Shape, 0},
                               {kOscMix, 0}, {kSubGain, 0}, {kFilterFreq, 127}, {kFilterAmount, 64}, {kUnisonOn, 0},
                               {kDelayOn, 0}, {kChorusOn, 0}, {kEffectOn, 0}})
        s.SetParam(id, v);
    const auto stepLevels = [&s] { // rms in the middle of each sounding run
        std::vector<float> l(48000), r(48000);
        s.NoteOn(60, 100);
        s.Render(l.data(), r.data(), 48000);
        s.NoteOff(60);
        Run(s, 9600);
        std::vector<double> levels;
        for (size_t i = 0; i < l.size();) {
            size_t j = i;
            while (j < l.size() && l[j] != 0) ++j;
            if (j - i > 6000) {
                double sum = 0;
                for (size_t k = i + 2000; k < i + 6000; ++k) sum += double(l[k]) * l[k];
                levels.push_back(std::sqrt(sum / 4000));
            }
            i = j + 1;
        }
        return levels;
    };
    const std::vector<double> a = stepLevels(), b = stepLevels();
    OXO_EXPECT(a.size() >= 3 && !b.empty());
    if (a.size() < 3 || b.empty()) return;
    for (const double v : a) OXO_EXPECT(std::fabs(v - a[0]) < 0.01 * a[0]);
    OXO_EXPECT(std::fabs(b[0] - a[0]) > 0.05 * a[0]);
}

// Fuzz: notes, pedal and patch switches (play mode, polyphony, unison, arp)
// in random order; once every key and the pedal are up, no voice is held.
OXO_TEST(RandomPatchSwitchesUnderHeldKeysLeaveNoStuckNote) {
    uint32_t seed = 12345;
    const auto rnd = [&](int n) {
        seed = seed * 1664525u + 1013904223u;
        return int((seed >> 8) % uint32_t(n));
    };
    for (int round = 0; round < 200; ++round) {
        Synth s;
        s.SetSampleRate(48000);
        bool down[128] = {};
        for (int step = 0; step < 60; ++step) {
            const int key = 48 + rnd(12);
            switch (rnd(9)) {
            case 0: case 1: case 2: s.NoteOn(key, 100); down[key] = true; break;
            case 3: case 4: s.NoteOff(key); down[key] = false; break;
            case 5: s.SetParam(kPlayMode, rnd(3)); s.SetParam(kPolyphony, rnd(2) ? 1 : 1 + rnd(16)); break;
            case 6: s.SetParam(kUnisonOn, rnd(2)); break;
            case 7: s.SetParam(kArpOn, rnd(4) == 0); break;
            case 8: s.ControlChange(64, rnd(2) ? 127 : 0); break;
            }
            Run(s, 64 + rnd(2000));
        }
        for (int k = 0; k < 128; ++k)
            if (down[k]) s.NoteOff(k);
        s.ControlChange(64, 0);
        Run(s, 48000);
        const uint64_t v = s.VoiceStates();
        int held = 0;
        for (int i = 0; i < kMaxVoices; ++i) held += (v >> (2 * i) & 3) == Synth::kVoiceHeld;
        OXO_EXPECT_EQ(held, 0);
        if (held) return;
    }
}

// A patch change empties the delay: the echoes fade out (no click), then
// silence, and the delay echoes new notes again.
OXO_TEST(ClearDelayFadesOutEchoes) {
    const auto setup = [](Synth &s) {
        s.SetSampleRate(48000);
        s.SetTransport(120, 0, false);
        s.SetParam(kEffectOn, 0);
        s.SetParam(kChorusOn, 0);
        s.SetParam(kDelayOn, 1);
        s.SetParam(kDelayTime, 5); // 1/4 beat = 125 ms
        s.SetParam(kDelayFeedback, 120);
        s.SetParam(kDelayDryWet, 127); // echoes only
        s.SetParam(kDelaySpread, 64);
        s.SetParam(kDelayTone, 64);
        s.SetParam(kDelayType, 0);
        s.NoteOn(60, 127);
        Run(s, 4800);
        s.Panic();
        Run(s, 21000); // echoes only, no voice left; the clear lands inside an echo
    };
    const auto peak = [](const std::vector<float> &x, size_t from, size_t to) {
        float p = 0;
        for (size_t i = from; i < to; ++i) p = std::max(p, std::fabs(x[i]));
        return p;
    };
    std::vector<float> kl(4800), kr(4800), l(4800), r(4800);
    Synth kept;
    setup(kept);
    kept.Render(kl.data(), kr.data(), 4800);
    OXO_EXPECT(peak(kl, 2400, 4800) > 0.01f); // without a clear the echoes go on

    Synth s;
    setup(s);
    s.ClearDelay();
    s.Render(l.data(), r.data(), 4800);
    bool faded = true;
    for (size_t i = 0; i < 960; ++i) {
        faded &= std::fabs(l[i]) <= std::fabs(kl[i]) + 1e-6f;                       // never louder
        if (i < 48) faded &= std::fabs(l[i] - kl[i]) <= 0.1f * std::fabs(kl[i]) + 1e-6f; // starts at full level
    }
    OXO_EXPECT(faded);
    OXO_EXPECT(peak(kl, 0, 48) > 0.01f);
    OXO_EXPECT_EQ(peak(l, 960, 4800), 0.0f); // empty after the fade (< 20 ms)
    OXO_EXPECT_EQ(peak(r, 960, 4800), 0.0f);

    s.NoteOn(60, 127);
    Run(s, 4800);
    s.Panic();
    s.Render(l.data(), r.data(), 4800);
    OXO_EXPECT(peak(l, 0, 4800) > 0.01f); // echoes again
}

// Hosts with blocks shorter than 256 samples (AUM at 64 / 128, odd sizes):
// a released voice must still end (it never did, the grid restarted with
// every block).
OXO_TEST(ReleasedVoicesEndWithShortHostBlocks) {
    for (int block : {64, 128, 200, 256, 441, 512}) {
        Synth s;
        s.SetSampleRate(44100);
        s.SetParam(kAmpRelease, 0);
        s.NoteOn(60, 100);
        std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        const auto render = [&](int frames) {
            for (int done = 0; done < frames; done += block) {
                s.BeginBlock(block);
                s.Render(l.data(), r.data(), block);
            }
        };
        render(4410);
        s.NoteOff(60);
        render(44100);
        OXO_EXPECT_EQ(s.VoiceStates(), uint64_t(0));
    }
}
