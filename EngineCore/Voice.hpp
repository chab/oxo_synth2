#pragma once

// One Synth1 voice. Every law marked (measured) comes from the original's
// renders (tools/s1ref, reference/meta); the rest is still a placeholder.
//
// Signal path so far: osc1 (+sub, +8x detune, FM from osc2) / osc2 (sync,
// ring) -> linear mix -> [filter: TODO] -> amp ADSR * gain * velocity ->
// fixed one-pole lowpass (analog 22 kHz, bilinear without prewarp).

#include "Envelope.hpp"
#include "Filter.hpp"
#include "MeasuredTables.hpp"
#include "Params.hpp"
#include "WaveTables.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace oxo::s2 {

struct Rng {
    uint32_t state = 0x9E3779B9u;
    uint32_t NextU32() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double Unit() { return NextU32() * (1.0 / 4294967296.0); }      // [0, 1)
    float Bipolar() { return float(Unit() * 2.0 - 1.0); }           // [-1, 1)
};

inline constexpr double kOscAmplitude = 0.2;  // (measured) sine/saw/tri peak at gain 64, vel 127
// (measured, measure_noise.py) osc2 noise = a 1-bit sequence s[n] = s[n-13]
// ^ s[n-18] ^ s[n-19] ^ s[n-20] played as +-kNoiseLevel. Every voice slot
// starts it from kNoiseSeed (the 20 bits before the first output) and
// carries on across notes (frozen while idle) like the oscillators; a
// synced osc1 wrap restarts it.
inline constexpr double kNoiseLevel = 0.65039;
inline constexpr uint32_t kNoiseSeed = 0x70A6E;
inline constexpr int kDetuneOscs = 8;
inline constexpr double kFmRefInc = 261.6255653 / 44100.0; // (measured) FM deviation unit, cycles per sample
inline constexpr double kRingDetuneLimit = 1.7; // (fitted) ring + osc1 detune + sub: osc1 section limiter
inline constexpr double kDetuneOffsets[kDetuneOscs] = {-7, -5, -3, -1, 1, 3, 5, 7}; // x d cents (measured)

inline double NoteHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

// sin(2 pi ph) for ph in [0, 1]: folded to a quarter wave, Taylor to x^11
// (|error| < 6e-8 = -145 dB; std::sin costs several times more per detune
// copy).
OXO_INLINE double Sin2Pi(double ph) {
    const double x = ph - 0.5, a = std::fabs(x), r = 2 * M_PI * std::min(a, 0.5 - a), r2 = r * r;
    double s = -1.0 / 39916800.0;
    for (const double c : {1.0 / 362880.0, -1.0 / 5040.0, 1.0 / 120.0, -1.0 / 6.0, 1.0}) s = s * r2 + c;
    return std::copysign(s * r, -x);
}
// The same in float for 4 detune copies at once (|error| < 2e-7).
OXO_INLINE simd::F4 Sin2Pi4(const Phase64 *ph, int shift) {
    using namespace simd;
    const auto cycles = [&](int k) { return int32_t((ph[k] << shift) >> 40); };
    const F4 x = FromInts(cycles(0), cycles(1), cycles(2), cycles(3)) * Set(0x1p-24f) - Set(0.5f), a = Abs(x);
    const F4 r = Set(float(2 * M_PI)) * Min(a, Set(0.5f) - a), r2 = r * r;
    F4 s = MulAdd(Set(-1.0f / 39916800.0f), r2, Set(1.0f / 362880.0f));
    for (const float c : {-1.0f / 5040.0f, 1.0f / 120.0f, -1.0f / 6.0f, 1.0f}) s = MulAdd(s, r2, Set(c));
    return NegSignOf(s * r, x);
}

class Voice {
public:
    void Prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        tables_ = WaveTableSet::ForSampleRate(sampleRate);
        filter_.Prepare(sampleRate);
    }

    // (measured, measure_slot_phase.py) with osc phase 0 the original never
    // resets a voice slot's oscillators: they carry on from the slot's last
    // note (frozen while idle), from per-slot values set at creation.
    void InitSlot(int slot) {
        osc1Phase_ = tables::kSlotOsc1Phase[slot % 32];
        osc2Phase_ = tables::kSlotOsc2Phase[slot % 32];
        // (a phase of 1.0 in the table stays just below 1: the sub copies
        // start at half of it, 0.5, not 0)
        Phase64 copies[kDetuneOscs];
        for (int i = 0; i < kDetuneOscs; ++i)
            copies[i] = ToPhase64(std::min(double(tables::kSlotCopyPhase[(slot % 32) * kDetuneOscs + i]), 0x1.fffffffffffffp-1));
        SetCopyPhases(copies, false);
    }

    // glide: portamento from this voice's own last pitch (measured).
    // detuneCents / pan (-1..1) / pitchSemis / phase (cycles): this voice's
    // place in a unison stack.
    void Start(int note, int velocity, bool glide, const PatchValues &p, uint64_t age,
               double detuneCents = 0, double pan = 0, double pitchSemis = 0, double unisonPhase = 0) {
        unisonSemis_ = detuneCents / 100.0 + pitchSemis;
        unisonPan_ = pan;
        SetNote(note, glide);
        velocity_ = velocity;
        age_ = age;
        gate_ = true;
        const bool stolen = amp_.Active();
        amp_.Configure(sampleRate_, p[kAmpAttack], p[kAmpDecay], p[kAmpSustain], p[kAmpRelease]);
        amp_.Trigger();
        // (measured) the filter env shares the amp env's laws
        filterEnv_.Configure(sampleRate_, p[kFilterAttack], p[kFilterDecay], p[kFilterSustain], p[kFilterRelease],
                             Adsr::SustainLaw::Linear);
        filterEnv_.Trigger();
        // (measured) mod env = AD with the amp env's attack / decay laws
        modEnv_.Configure(sampleRate_, p[kModEnvAttack], p[kModEnvDecay], 0, 0, Adsr::SustainLaw::Linear);
        modEnv_.Trigger();
        unisonPhase_ = unisonPhase;
        StartPhases(p, !stolen);
        tail_ = false;
        // A stolen voice still sounding keeps its filter / output state:
        // zeroing them under a live signal clicks (the original doesn't).
        if (!stolen) filter_.Reset();
    }

    // Oscillator start phases (Start; a mono retrigger restarts them too).
    // relockSub: false = a free-running (osc phase 0) sub keeps its phase
    // (re-locking it on a stolen, sounding voice clicks).
    void StartPhases(const PatchValues &p, bool relockSub = true) {
        // (measured) osc phase 0 = free: the slot's phases carry on (InitSlot);
        // > 0 = osc1 starts at phase 0, osc2 at (v - 1) / 126 of half a cycle
        // (127 = 180 degrees), both shifted by the unison phase.
        Phase64 copies[kDetuneOscs];
        for (int i = 0; i < kDetuneOscs; ++i) copies[i] = CopyPhase(i);
        if (p[kOscPhase] > 0) {
            osc1Phase_ = Wrap(unisonPhase_);
            osc2Phase_ = Wrap(0.5 * (p[kOscPhase] - 1) / 126.0 + unisonPhase_);
            // (measured) copy i starts kCopyPhaseDeg[i] * (v - 1) / 126 after osc1
            static constexpr double kCopyPhaseDeg[kDetuneOscs] = {321, 172, 291, 202, 0, 69, 210, 126};
            for (int i = 0; i < kDetuneOscs; ++i)
                copies[i] = ToPhase64(osc1Phase_ + kCopyPhaseDeg[i] / 360.0 * (p[kOscPhase] - 1) / 126.0);
        }
        // (measured) the sub runs phase-locked to osc1: its harmonic at osc1's
        // pitch is in phase with osc1's fundamental.
        if (relockSub || p[kOscPhase] > 0) {
            subPhase_ = osc1Phase_ * (p[kSubOctave] ? 0.5 : 1.0);
            SetCopyPhases(copies, p[kSubOctave] != 0);
        }
    }

    // Mono / legato note change on a sounding voice.
    void ChangeNote(int note, int velocity, bool retrigger, bool glide) {
        SetNote(note, glide);
        gate_ = true;
        if (retrigger) {
            if (velocity >= 0) velocity_ = velocity; // < 0: keep the current one
            amp_.Trigger();
            filterEnv_.Trigger();
            modEnv_.Trigger();
        }
    }

    void Release() {
        gate_ = false;
        amp_.ReleaseNote();
        filterEnv_.ReleaseNote();
    }
    void Kill() {
        amp_.Reset();
        tail_ = false;
    }
    // Filter state cleared (a patch switch, at silence).
    void ClearFilters() { filter_.Reset(); }

    // (measured, measure_noise.py) a released voice ends at the first
    // 256-sample block boundary after its envelope fell silent (-66 dB), or
    // at the host block's end when that comes first (blocks < 256 samples).
    bool Active() const { return amp_.Active() || tail_; }
    bool Gate() const { return gate_; }
    int Note() const { return note_; }
    uint64_t Age() const { return age_; }

    // lfo[k] = LFO k's per-sample value (nullptr when that LFO is off).
    // cutoffKnob / cutoffStep: the smoothed cutoff knob at the block start and
    // its per-sample ramp (Synth's 50 ms glide, measured).
    // pos: the chunk's first sample in the host block; blockEnd: the host
    // block's length.
    void Render(float *left, float *right, int frames, int64_t pos, int64_t blockEnd, const PatchValues &p,
                double bendSemis, const double *const lfo[2], double cutoffKnob, double cutoffStep) {
        if (!Active()) return;
        if (p[kPortaTime] <= 0) glide_ = note_;
        // (measured, measure_porta.py) a glide moves every 16 samples
        for (int off = 0; off < frames;) {
            const bool gliding = glide_ != note_;
            const int n = gliding ? std::min(frames - off, glideTick_) : frames - off;
            const double *at[2] = {lfo[0] ? lfo[0] + off : nullptr, lfo[1] ? lfo[1] + off : nullptr};
            RenderChunk(left + off, right + off, n, pos + off, blockEnd, p, bendSemis, at, cutoffKnob + cutoffStep * off, cutoffStep);
            off += n;
            if (gliding && (glideTick_ -= n) == 0) {
                glideTick_ = 16;
                // r -> r * a - E * (1 - a): exponential, aimed E past the target (lands)
                const double r = glide_ - note_, a = std::exp(-16.0 / (tables::kPortaTau[p[kPortaTime]] * sampleRate_));
                const double rest = std::max(std::abs(r) * a - glideAim_ * (1 - a), 0.0);
                glide_ = note_ + (r < 0 ? -rest : rest);
            }
        }
    }

private:
    void RenderChunk(float *left, float *right, int frames, int64_t pos, int64_t blockEnd, const PatchValues &p,
                     double bendSemis, const double *const lfo[2], double cutoffKnob, double cutoffStep) {
        // ---- LFO routing (measured depth laws per destination). Both LFOs on
        // one destination (measured, measure_lfo_sum.py): pitch = sum of each
        // LFO's cents; cutoff / FM / pw / pan = the law applied to the
        // depth-weighted sum of the LFO values; amp = product of the gains.
        LfoRoute r[8];
        for (int k = 0; k < 2; ++k) {
            if (!lfo[k]) continue;
            LfoRoute &route = r[std::clamp<int>(p[k ? kLfo2Dest : kLfo1Dest], 1, 7)];
            route.v[route.n] = lfo[k];
            route.depth[route.n] = p[k ? kLfo2Depth : kLfo1Depth];
            route.scale[route.n] = route.depth[route.n] / 127.0;
            ++route.n;
        }
        const auto pitchCents = [&](const LfoRoute &route, int i) {
            double c = 0;
            for (int k = 0; k < route.n; ++k) c += tables::kLfoPitchCents[route.depth[k]] * route.v[k][i];
            return c;
        };
        // ---- per-block parameters ----
        const double tuneSemis = tables::kFineCents[p[kOscFineTune]] / 100.0;
        // (measured) a glide plays in 1/128-octave steps: target + ceil(r / step)
        const double gap = glide_ - note_;
        const double played = note_ + (gap < 0 ? -1 : 1) * kGlideStep * std::ceil(std::abs(gap) / kGlideStep - 1e-9);
        // bend + LFO pitch: added per sample, quantized (PitchMod)
        const double base = played + p[kOscKeyShift] + tuneSemis + unisonSemis_;
        const double f1 = NoteHz(base);
        const double osc2Note = (p[kOsc2KbdTrack] ? base : 57.0 + tuneSemis) +
                                tables::kOsc2PitchSemis[p[kOsc2Pitch]] + tables::kFineCents[p[kOsc2Fine]] / 100.0;
        const double f2 = NoteHz(osc2Note);
        const int t1 = int(std::lround(base + bendSemis)), t2 = int(std::lround(osc2Note + bendSemis));
        const double bendCents = bendSemis * 100.0, bendRatio = std::exp2(PitchMod(bendCents) / 1200.0);
        const bool osc1Silent = t1 > 127, osc2Silent = t2 > 127; // (measured) no table above note 127
        const WaveTable &saw1 = tables_->Saw(std::clamp(t1, 0, 127));
        const WaveTable &tri1 = tables_->Triangle(std::clamp(t1, 0, 127));
        const int tSub = p[kSubOctave] ? t1 - 12 : t1;
        const WaveTable &sawSub = tables_->Saw(std::clamp(tSub, 0, 127));
        const WaveTable &triSub = tables_->Triangle(std::clamp(tSub, 0, 127));
        const WaveTable &saw2 = tables_->Saw(std::clamp(t2, 0, 127));
        const WaveTable &tri2 = tables_->Triangle(std::clamp(t2, 0, 127));
        const double inc1 = f1 / sampleRate_, inc2 = f2 / sampleRate_;
        const double width = p[kOscPulseWidth] / 254.0; // (measured) duty of the low part, 127 = square
        const double mix2 = p[kOscMix] / 127.0, mix1 = 1.0 - mix2; // (measured) linear crossfade
        const double fmBeta = p[kOsc2Ring] ? 0.0 : tables::kFmBeta[p[kOsc1FM]];
        const bool ring = p[kOsc2Ring] != 0, sync = p[kOsc2Sync] != 0;
        const int shape1 = p[kOsc1Shape], shape2 = p[kOsc2Shape];
        const double subGainKnob = p[kSubGain];
        const double subW = subGainKnob / (subGainKnob + 32.0), mainW = 1.0 - subW; // (measured) auto level
        const bool subOn = p[kSubGain] > 0;
        // (measured, measure_osc_mix.py) with the sub on, osc2 also takes the
        // main weight: osc1 (1-w)(1-m), sub w(1-m), osc2 (1-w)m, over 1 - wm
        const double mixNorm = 1.0 / (1.0 - mix2 * subW), mixA = mix1 * mixNorm, mixB = mix2 * mainW * mixNorm;
        const int subShape = p[kSubShape];
        const double subRatio = p[kSubOctave] ? 0.5 : 1.0;
        const bool detuneOn = p[kOsc1Detune] > 0;
        if (p[kOsc1Detune] != detuneKnob_) {
            detuneKnob_ = p[kOsc1Detune];
            for (int i = 0; i < kDetuneOscs; ++i)
                detuneRatio_[i] = std::exp2(kDetuneOffsets[i] * (20.0 * p[kOsc1Detune] / 127.0) / 1200.0);
        }
        const double *detuneRatio = detuneRatio_;
        const double detuneGain = shape1 == 0 ? 0.288 : 0.297; // (measured) per detuned copy

        // ---- cutoff modulation in knob units (measured): kbd track pivots on
        // key 48 (1 octave = 12.624 knob units), env amount (v - 64) * 1.984
        // knob units at env = 1, scaled by velocity when the switch is on.
        // (measured, measure_filter_env.py) the env sweeps from base - 2.1
        // (env 0) to the env-1 TARGET clamped to [-2, 125]: a target beyond
        // the top still moves the cutoff over the whole attack / decay.
        const int filterType = p[kFilterType], filterRes = p[kFilterRes];
        const double cutoffBase = cutoffKnob + tables::kKbdTrack[p[kFilterKbdTrack]] * (note_ - 48) * (12.624 / 12.0);
        const double envDepth = (p[kFilterAmount] - 64) * 1.984 *
                                (p[kFilterVelSwitch] ? tables::kFilterVelScale[velocity_] : 1.0);

        filterEnv_.Configure(sampleRate_, p[kFilterAttack], p[kFilterDecay], p[kFilterSustain], p[kFilterRelease],
                             Adsr::SustainLaw::Linear);
        amp_.Configure(sampleRate_, p[kAmpAttack], p[kAmpDecay], p[kAmpSustain], p[kAmpRelease]);
        const double satA = tables::kSaturationA[p[kFilterSat]];
        modEnv_.Configure(sampleRate_, p[kModEnvAttack], p[kModEnvDecay], 0, 0, Adsr::SustainLaw::Linear);
        const bool modOn = p[kModEnvOn] != 0;
        const int modDest = p[kModEnvDest];
        // (measured) amount -> -1..+1: (amt - 64) / 63 above 64, / 64 below
        const double modAmt = (p[kModEnvAmount] - 64) / (p[kModEnvAmount] > 64 ? 63.0 : 64.0);

        const double ampGain = kOscAmplitude * tables::kAmpGain[p[kAmpGain]] *
                               std::pow(10.0, -15.0 * (p[kAmpVelSens] / 64.0) * (127 - velocity_) / 126.0 / 20.0);

        double curWidth = width;
        Phase64 curWidth64 = 0;
        // a sub-octave change on a sounding voice re-locks the sub copies
        if (copyHalf_ != (p[kSubOctave] != 0)) {
            Phase64 copies[kDetuneOscs];
            for (int d = 0; d < kDetuneOscs; ++d) copies[d] = CopyPhase(d);
            SetCopyPhases(copies, p[kSubOctave] != 0);
        }
        const int copyShift = copyHalf_ ? 1 : 0;
        // the sub's shapes as osc1's (0 sine, 1 saw, 2 pulse, 3 tri): sine, tri, saw, square
        static constexpr int kSubAsOsc1[4] = {0, 3, 1, 2};
        const int subAs1 = kSubAsOsc1[subShape & 3];
        // the stored copy phases' step ratios (in Phase64 units / 2, see the phase update)
        const double copyRatio = copyHalf_ ? 0.5 : 1.0;
        double copyScale[kDetuneOscs];
        for (int d = 0; d < kDetuneOscs; ++d) copyScale[d] = copyRatio * detuneRatio[d] * 0x1p63;
        // copyScale <= 2^63 * 2^(140 / 1200): |step| below this stays under one cycle
        static constexpr double kFastStepLimit = 0.9;

        // (measured, key-synced square LFO: measure_lfo_keysync.py /
        // measure_lfo_cutoff.py) cutoff, amp and pan are UNIPOLAR, u =
        // (lfo + 1) / 2 (linear in between assumed):
        // cutoff -> pulled toward knob 125 by (depth / 127) * u;
        // amp    -> gain 1 - (depth / 127) * (1 - u);
        // pan    -> position q = 1 - (1 - s)^3 for s = depth / 127 * lfo >= 0
        //           (odd), gains toward side 1 + |q| / 2, away side 1 - |q|.
        // pw     -> (measured, measure_lfo_pw.py) the low-part duty moves toward
        //           1 (lfo > 0) or 0 (lfo < 0) by depth / 127 * |lfo| of the
        //           room left on that side: never wraps, +-0.5 for a square.

        // balance law (measured on unison spread): centre 1/1, hard side 1/0.
        // (measured, check_unison.py) patch pan (q = (pan - 64) / 64 below
        // the centre, / 63 above) and the unison spread MULTIPLY their gains.
        double panL = 1, panR = 1;
        for (const double q : {(p[kPan] - 64) / (p[kPan] < 64 ? 64.0 : 63.0), unisonPan_}) {
            panL *= std::min(1.0, 1.0 - q);
            panR *= std::min(1.0, 1.0 + q);
        }

        PitchRatio ratioAll, ratio2;
        double fmIncA = NAN, fmRef = 0; // kFmRefInc / incA, recomputed when incA changes
        for (int i = 0; i < frames; ++i) {
            // ---- per-sample modulation ----
            const double me = modOn ? modEnv_.Next() * modAmt : 0.0;
            const double lfoAll = r[2] ? pitchCents(r[2], i) : 0.0;
            const double mAll = r[2] ? ratioAll(bendCents + lfoAll) : bendRatio;
            const double m2 = r[1] ? ratio2(bendCents + lfoAll + pitchCents(r[1], i)) : mAll;
            // (measured) mod env -> osc2 pitch: +-60 semitones at env 1 (not quantized)
            const double mEnv2 = (modOn && modDest == 0) ? std::exp2(me * 60.0 / 12.0) : 1.0;
            const double incA = inc1 * mAll, incB = inc2 * m2 * mEnv2;
            double w = width;
            if (r[5]) {
                const double l = r[5].Signed(i);
                w += l * (l > 0 ? 1.0 - width : width);
            }
            if (modOn && modDest == 2) {
                // (measured, measure_modenv_pw.py) same room-left law
                const double m = std::clamp(me, -1.0, 1.0);
                w += m * (m > 0 ? 1.0 - w : w);
            }
            // (measured, measure_pw_limits.py) the final duty never reaches 0 / 1:
            // pw 0 = pw 1 = 1.25 / 254, modulation tops out at 1 - 1.375 / 254
            w = std::clamp(w, 1.25 / 254.0, 1.0 - 1.375 / 254.0);
            double fmB = fmBeta;
            if (!ring && (r[6] || (modOn && modDest == 1))) {
                // (measured, measure_fm_mod.py) in FM knob units (fractional):
                // LFO unipolar toward 127 by depth / 127 * u; mod env
                // bipolar room-left, m = (amt - 64) / 64 at env 1.
                double knob = p[kOsc1FM];
                if (r[6]) knob += (127.0 - knob) * r[6].Unipolar(i);
                if (modOn && modDest == 1) {
                    // the DECAY reaches FM in dB: 0 dB -> 1, -80 dB -> 0 (the
                    // offset falls LINEARLY in time, to 0 at 9.2 taus); the
                    // attack ramp is linear as elsewhere
                    const double e = modEnv_.Level();
                    const double g = modEnv_.Attacking() ? e : e > 1e-4 ? 1.0 + std::log(e) / std::log(1e4) : 0.0;
                    const double m = std::clamp(modAmt * g, -1.0, 1.0);
                    knob += m * (m > 0 ? 127.0 - knob : knob);
                }
                const double k = std::clamp(knob, 0.0, 127.0);
                const int j = std::min(int(k), 126);
                fmB = tables::kFmBeta[j] + (tables::kFmBeta[j + 1] - tables::kFmBeta[j]) * (k - j);
            }
            curWidth = w;
            if (detuneOn && shape1 == 2) curWidth64 = ToPhase64(w);
            // ---- osc2 ----
            double o2 = 0;
            switch (shape2) {
            case 1: o2 = saw2.Read(osc2Phase_); break;
            case 2: o2 = 0.5 * (saw2.Read(osc2Phase_) - saw2.Read(Wrap(osc2Phase_ + w))); break;
            case 3: o2 = tri2.Read(osc2Phase_); break;
            default: {
                const uint32_t bit = (noise_ >> 12 ^ noise_ >> 17 ^ noise_ >> 18 ^ noise_ >> 19) & 1;
                noise_ = (noise_ << 1 | bit) & 0xFFFFF;
                o2 = bit ? kNoiseLevel : -kNoiseLevel;
                break;
            }
            }
            if (osc2Silent && shape2 != 4) o2 = 0;

            // ---- osc1 (+ detune copies, + sub) ----
            // (measured, measure_fm_key.py) the deviation is a FIXED phase step
            // per sample (beta x key 60 at 44.1 kHz): the index falls an octave
            // up, rises with the sample rate
            if (incA != fmIncA) fmIncA = incA, fmRef = kFmRefInc / incA;
            const double fm = 1.0 + fmB * o2 * fmRef;
            double o1 = OscRead(shape1, saw1, tri1, osc1Phase_, curWidth);
            double sub = subOn ? OscRead(subAs1, sawSub, triSub, subPhase_, 0.5) : 0.0;
            if (detuneOn) {
                const double acc = o1 + CopiesSum(shape1, saw1, tri1, copyPhase_.data(), copyShift, curWidth64);
                const double subAcc = subOn ? sub + CopiesSum(subAs1, sawSub, triSub, copyPhase_.data(), 0, Phase64(1) << 63) : sub;
                // a step under one cycle either way converts directly (two's
                // complement = mod 1)
                const double step = incA * fm;
                if (std::fabs(step) < kFastStepLimit) {
                    for (int d = 0; d < kDetuneOscs; ++d) copyPhase_[d] += Phase64(int64_t(step * copyScale[d])) << 1;
                } else {
                    for (int d = 0; d < kDetuneOscs; ++d) copyPhase_[d] += ToPhase64(step * copyRatio * detuneRatio[d]);
                }
                o1 = acc * detuneGain;
                // (measured, measure_ring_detune.py) ring on: the sub copies
                // skip the copy gain (~+10 dB), see the limiter below
                sub = ring ? subAcc : subAcc * detuneGain;
            }
            if (osc1Silent) o1 = sub = 0;
            double osc1Out = subOn ? mainW * o1 + subW * sub : o1;
            // TODO(measure): the original bounds that loud sum; a tanh at 1.7
            // fits to -15..-22 dB (it also has some osc1 x sub products)
            if (ring && detuneOn && subOn) osc1Out = kRingDetuneLimit * std::tanh(osc1Out / kRingDetuneLimit);
            if (ring) o2 *= o1;

            osc1Phase_ += incA * fm;
            const bool wrapped = osc1Phase_ >= 1.0 || osc1Phase_ < 0.0;
            osc1Phase_ = Wrap(osc1Phase_);
            subPhase_ = Wrap(subPhase_ + incA * subRatio * fm);
            if (sync && wrapped && fm > 0) {
                // (measured, alias energy matches to 0.1 dB) the original's hard
                // sync is NAIVE: osc2 restarts at phase 0, no band-limiting
                osc2Phase_ = 0;
                // (measured, v1.12 manual "synced noise") the noise generator
                // restarts too, from one fixed state shared by every voice and
                // note: synced noise repeats per osc1 cycle (pitched)
                noise_ = kNoiseSeed;
            } else {
                osc2Phase_ = Wrap(osc2Phase_ + incB);
            }

            const double mixed = mixA * osc1Out + mixB * o2;
            // ---- filter -> saturation -> amp (measured order) ----
            const double base = cutoffBase + cutoffStep * i, low = base - 2.1;
            double cutoff = low + (std::clamp(base + envDepth, -2.0, 125.0) - low) * filterEnv_.Next();
            if (r[3]) cutoff += std::max(0.0, 125.0 - cutoff) * std::min(1.0, r[3].Unipolar(i)); // tops out (measured)
            filter_.SetParams(filterType, cutoff, filterRes);
            const double filtered = Saturate(filter_.Process(mixed), satA);
            const double env = amp_.Next();
            double gainMod = 1.0;
            for (int k = 0; k < r[4].n; ++k) gainMod *= 1.0 - r[4].scale[k] * 0.5 * (1.0 - r[4].v[k][i]);
            const double x = filtered * env * ampGain * gainMod;
            const double y = x; // (the original's 22 kHz output lowpass = the EQ tone stage)
            double gl = panL, gr = panR;
            if (r[7]) {
                const double s = r[7].Signed(i); // + = left; unclamped (measured: 127 + 127 -> gains 2 / -1)
                const double q = s >= 0 ? 1.0 - std::pow(1.0 - s, 3.0) : std::pow(1.0 + s, 3.0) - 1.0;
                gl *= q >= 0 ? 1.0 + 0.5 * q : 1.0 + q;
                gr *= q >= 0 ? 1.0 - q : 1.0 - 0.5 * q;
            }
            left[i] += float(y * gl);
            right[i] += float(y * gr);
            if (!amp_.Active() && (((pos + i + 1) & 255) == 0 || pos + i + 1 >= blockEnd)) {
                tail_ = false;
                break;
            }
            tail_ = !amp_.Active();
        }
    }

    // osc1's shape (0 sine, 1 saw, 2 pulse of duty `width`, 3 tri) at phase ph
    static OXO_INLINE double OscRead(int shape, const WaveTable &saw, const WaveTable &tri, double ph, double width) {
        switch (shape) {
        case 0: return Sin2Pi(ph);
        case 1: return saw.Read(ph);
        case 2: return 0.5 * (saw.Read(ph) - saw.Read(Wrap(ph + width)));
        default: return tri.Read(ph);
        }
    }
    // the same summed over the 8 detune copies' phases ph << shift, 4 at a time (Simd.hpp)
    static OXO_INLINE double CopiesSum(int shape, const WaveTable &saw, const WaveTable &tri, const Phase64 *ph, int shift,
                                       Phase64 width) {
        using simd::Sum;
        switch (shape) {
        case 0: return Sum(Sin2Pi4(ph, shift) + Sin2Pi4(ph + 4, shift));
        case 1: return Sum(saw.Read4(ph, shift) + saw.Read4(ph + 4, shift));
        case 2:
            return 0.5f * Sum((saw.Read4(ph, shift) - saw.Read4(ph, shift, width)) +
                              (saw.Read4(ph + 4, shift) - saw.Read4(ph + 4, shift, width)));
        default: return Sum(tri.Read4(ph, shift) + tri.Read4(ph + 4, shift));
        }
    }

    // The LFOs (0..2) routed to one destination.
    struct LfoRoute {
        const double *v[2] = {nullptr, nullptr};
        int depth[2] = {0, 0};
        double scale[2] = {0, 0}; // depth / 127
        int n = 0;
        explicit operator bool() const { return n > 0; }
        // sum of depth / 127 * f(lfo value)
        template <typename F> double Sum(int i, F f) const {
            double s = 0;
            for (int k = 0; k < n; ++k) s += scale[k] * f(v[k][i]);
            return s;
        }
        double Signed(int i) const { return Sum(i, [](double x) { return x; }); }
        double Unipolar(int i) const { return Sum(i, [](double x) { return 0.5 * (x + 1.0); }); }
    };

    // ph - floor(ph); the floor (a library call on x64 without SSE4.1) only
    // when ph is out of [0, 1)
    static OXO_INLINE double Wrap(double ph) { return ph >= 0.0 && ph < 1.0 ? ph : ph - std::floor(ph); }
    // osc1's detune copy d (the sub's copy d = copyPhase_[d])
    Phase64 CopyPhase(int d) const { return copyHalf_ ? copyPhase_[d] << 1 : copyPhase_[d]; }
    // osc1's copy phases, the sub's locked to them (half = sub octave)
    void SetCopyPhases(const Phase64 *osc1Copies, bool half) {
        copyHalf_ = half;
        for (int d = 0; d < kDetuneOscs; ++d) copyPhase_[d] = half ? osc1Copies[d] >> 1 : osc1Copies[d];
    }
    // (measured, measure_porta.py) a glide aims 1.35 % of its starting
    // distance past the target (the curve scales with the interval)
    static constexpr double kGlideStep = 12.0 / 128, kGlideAim = 0.0135; // semitones, x distance
    // (measured, measure_pitch_mod.py) pitch bend + LFO pitch (cents) play in
    // 1/128-octave steps, truncated toward 0 (the mod env's pitch does not)
    static double PitchMod(double cents) { return 9.375 * std::trunc(cents / 9.375); }
    // exp2(PitchMod(cents) / 1200), recomputed only when the step changes
    struct PitchRatio {
        double steps = NAN, ratio = 1;
        double operator()(double cents) {
            if (const double s = std::trunc(cents / 9.375); s != steps) steps = s, ratio = std::exp2(9.375 * s / 1200.0);
            return ratio;
        }
    };

    void SetNote(int note, bool glide) {
        note_ = note;
        if (!glide) glide_ = note;
        glideAim_ = kGlideAim * std::abs(glide_ - note);
        glideTick_ = 16;
    }

    double sampleRate_ = 44100;
    std::shared_ptr<const WaveTableSet> tables_;
    Adsr amp_;
    Adsr filterEnv_;
    Adsr modEnv_;
    VoiceFilter filter_;
    int note_ = 0, velocity_ = 0;
    double unisonSemis_ = 0, unisonPan_ = 0, unisonPhase_ = 0;
    double glide_ = 0; // gliding pitch (semis); (measured) a never-used voice starts from note 0
    double glideAim_ = 0;
    int glideTick_ = 16;
    uint64_t age_ = 0;
    bool gate_ = false;
    double osc1Phase_ = 0, osc2Phase_ = 0, subPhase_ = 0;
    // the detune copies' phases, shared by osc1's and the sub's copies:
    // copyHalf_ = they run at the sub's half rate (sub octave), osc1's copy
    // phases = copyPhase_ << 1 (exact: the sub stays locked for free)
    std::array<Phase64, kDetuneOscs> copyPhase_{};
    bool copyHalf_ = false;
    int detuneKnob_ = -1; // the osc1 detune detuneRatio_ holds (RenderChunk)
    double detuneRatio_[kDetuneOscs] = {};
    uint32_t noise_ = kNoiseSeed;
    bool tail_ = false; // envelope done, running silent to the block boundary
};

} // namespace oxo::s2
