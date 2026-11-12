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
// (measured, locked noise sync: osc1's backward pitch at FM 70..127) as an
// FM source the noise is +-0.6500, 0.06 % under its audio level
inline constexpr double kNoiseFm = 0.65;
// (measured: the reset times of synced noise over seconds, keys 30..72) the
// original steps OSC1 in whole 2^-27 cycles (a 2048-point index + 16-bit
// fraction), each sample's step (FM included) TRUNCATED: its pitch sits up
// to 7e-9 cycle / sample under the exact one, which decides on which sample
// a sync lands. Osc2 does not (FM nulls 9 dB worse with it truncated too).
inline constexpr double kPhaseUnits = 134217728.0;
OXO_INLINE double PhaseStep(double inc) { return double(int64_t(inc * kPhaseUnits)) * (1.0 / kPhaseUnits); }
inline constexpr uint32_t kNoiseSeed = 0x70A6E;
inline constexpr int kDetuneOscs = 8;
inline constexpr double kFmRefInc = 261.6255653 / 44100.0; // (measured) FM deviation unit, cycles per sample
// (fitted, sync with FM 0, osc2 alone: residual -6 dB at keys 36..72, osc2
// saw / pulse / tri, pitch -12..+12; best 4.5e-5..8e-5 per case) a sync on
// osc1's forward wrap restarts osc2 a hair AFTER phase 0: on a saw that sample
// reads the steep side of the band-limited jump, not its midpoint. Under sync
// + FM it decides whether osc1 escapes a backward lock. The syncs FM's
// backward steps fire keep phase 0 (that offset there: worse above FM ~110).
inline constexpr double kSyncResetPhase = 7e-5;
inline constexpr double kDetuneOffsets[kDetuneOscs] = {-7, -5, -3, -1, 1, 3, 5, 7}; // x d cents (measured)

inline double NoteHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

// sin(2 pi ph) for ph in [0, 1]: folded to a quarter wave, Taylor to x^11
// (|error| < 6e-8 = -145 dB; std::sin costs several times more).
OXO_INLINE double Sin2Pi(double ph) {
    const double x = ph - 0.5, a = std::fabs(x), r = 2 * M_PI * std::min(a, 0.5 - a), r2 = r * r;
    double s = -1.0 / 39916800.0;
    for (const double c : {1.0 / 362880.0, -1.0 / 5040.0, 1.0 / 120.0, -1.0 / 6.0, 1.0}) s = s * r2 + c;
    return std::copysign(s * r, -x);
}
class Voice {
public:
    // The longest run rendered in one go (Synth renders in chunks of it:
    // the LFOs' buffers, the voice's per-chunk modulation arrays).
    static constexpr int kMaxChunk = 64;

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
    // detuneCents / pan (-1..1) / pitchSemis / unisonIndex (k-th voice
    // started) + unisonShift (phase knob / 127): this voice's place in a
    // unison stack.
    void Start(int note, int velocity, bool glide, const PatchValues &p, uint64_t age,
               double detuneCents = 0, double pan = 0, double pitchSemis = 0, int unisonIndex = 0,
               double unisonShift = 0) {
        unisonSemis_ = detuneCents / 100.0 + pitchSemis;
        unisonPan_ = pan;
        SetNote(note, glide);
        velocity_ = velocity;
        age_ = age;
        gate_ = true;
        const bool stolen = amp_.Active();
        amp_.Configure(sampleRate_, p[kAmpAttack], p[kAmpDecay], p[kAmpSustain], p[kAmpRelease]);
        amp_.SetQuantized(true);
        amp_.Trigger();
        ampSince_ = 0, ampLeft_ = 0;
        // (measured) the filter env shares the amp env's laws
        filterEnv_.Configure(sampleRate_, p[kFilterAttack], p[kFilterDecay], p[kFilterSustain], p[kFilterRelease],
                             Adsr::SustainLaw::Linear);
        filterEnv_.Trigger();
        // (measured) mod env = AD with the amp env's attack / decay laws
        modEnv_.Configure(sampleRate_, p[kModEnvAttack], p[kModEnvDecay], 0, 0, Adsr::SustainLaw::Linear);
        modEnv_.Trigger();
        unisonIndex_ = unisonIndex % 32, unisonShift_ = unisonShift;
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
        // (127 = 180 degrees), each shifted by the unison phase.
        Phase64 copies[kDetuneOscs];
        bool past[kDetuneOscs] = {}; // a copy's start is past one cycle
        for (int i = 0; i < kDetuneOscs; ++i) copies[i] = CopyPhase(i);
        if (p[kOscPhase] > 0) {
            // (measured, measure_unison_shift.py) the unison phase moves EVERY
            // oscillator of the k-th stack voice by its own slot table's phase
            // at slot k (0..1 cycle, not wrapped: it scales with the knob),
            // times v / 127; voice 0 too (osc1's slot 0 phase is ~0)
            const int k = unisonIndex_;
            const auto shift = [&](const float *table, int stride, int i) {
                return double(table[k * stride + i]) * unisonShift_;
            };
            osc1Phase_ = Wrap(shift(tables::kSlotOsc1Phase, 1, 0));
            osc2Phase_ = Wrap(0.5 * (p[kOscPhase] - 1) / 126.0 + shift(tables::kSlotOsc2Phase, 1, 0));
            // (measured) copy i starts kCopyPhaseDeg[i] * (v - 1) / 126 after osc1's start
            static constexpr double kCopyPhaseDeg[kDetuneOscs] = {321, 172, 291, 202, 0, 69, 210, 126};
            for (int i = 0; i < kDetuneOscs; ++i) {
                const double c = kCopyPhaseDeg[i] / 360.0 * (p[kOscPhase] - 1) / 126.0 +
                                 shift(tables::kSlotCopyPhase, kDetuneOscs, i);
                copies[i] = ToPhase64(c), past[i] = c >= 1.0;
            }
        }
        // (measured) the sub runs phase-locked to osc1: its harmonic at osc1's
        // pitch is in phase with osc1's fundamental.
        if (relockSub || p[kOscPhase] > 0) {
            subPhase_ = osc1Phase_ * (p[kSubOctave] ? 0.5 : 1.0);
            SetCopyPhases(copies, p[kSubOctave] != 0);
            // (measured) an octave-down sub's copy starts at HALF its osc1 copy's
            // unwrapped start: one past a cycle (unison phase) = half a sub cycle on
            if (copyHalf_)
                for (int i = 0; i < kDetuneOscs; ++i)
                    if (past[i]) copyPhase_[i] += Phase64(1) << 63;
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

    // lfo[k] = LFO k's per-sample value (nullptr when that LFO is off);
    // lfoSteps[k] = its pitch steps when it is routed to pitch (Lfo.hpp).
    // cutoffKnob / cutoffStep: the smoothed cutoff knob at the block start and
    // its per-sample ramp (Synth's 50 ms glide, measured).
    // pos: the chunk's first sample in the host block; blockEnd: the host
    // block's length.
    void Render(float *left, float *right, int frames, int64_t pos, int64_t blockEnd, const PatchValues &p,
                double bendSemis, const double *const lfo[2], const int32_t *const lfoSteps[2], double cutoffKnob,
                double cutoffStep) {
        if (!Active()) return;
        if (p[kPortaTime] <= 0) glide_ = note_;
        // (measured, measure_porta.py) a glide moves every 16 samples
        for (int off = 0; off < frames;) {
            const bool gliding = glide_ != note_;
            const int n = std::min(gliding ? std::min(frames - off, glideTick_) : frames - off, kMaxChunk);
            const double *at[2] = {lfo[0] ? lfo[0] + off : nullptr, lfo[1] ? lfo[1] + off : nullptr};
            const int32_t *stepsAt[2] = {lfoSteps[0] ? lfoSteps[0] + off : nullptr, lfoSteps[1] ? lfoSteps[1] + off : nullptr};
            RenderChunk(left + off, right + off, n, pos + off, blockEnd, p, bendSemis, at, stepsAt, cutoffKnob + cutoffStep * off,
                        cutoffStep);
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

    // LFO k's destination (1..7), 0 = no route. (measured, bend on a step +
    // a square LFO on pitch) depth 0 = no modulation at all, whatever the
    // destination: no route
    static int LfoDest(const PatchValues &p, const double *const lfo[2], int k) {
        return lfo[k] && p[k ? kLfo2Depth : kLfo1Depth] > 0 ? std::clamp<int>(p[k ? kLfo2Dest : kLfo1Dest], 1, 7) : 0;
    }

private:
    // The per-sample loop is compiled once per case (RenderChunkAs), so each
    // holds only what its case computes: with every feature as a run-time
    // test it carried twice the instructions (and live values to spill).
    //   kOsc     what moves the oscillators within the chunk (OscKind)
    //   kDetune  osc1 detune on (its 8 copies)
    //   kFilter  the filter structure (VoiceFilter::Kind)
    // Same statements in every case, so the cases render bit-identically.
    enum OscKind {
        kOscStatic, // nothing: steps and pulse width are constants
        kOscMoving, // mod env or LFOs on pitch / pulse width
        kOscCross,  // FM, ring or sync (osc1 and osc2 depend on each other), anything else too
    };
    void RenderChunk(float *left, float *right, int frames, int64_t pos, int64_t blockEnd, const PatchValues &p,
                     double bendSemis, const double *const lfo[2], const int32_t *const lfoSteps[2], double cutoffKnob,
                     double cutoffStep) {
        const bool modEnv = p[kModEnvOn] != 0;
        bool cross = p[kOsc2Ring] || p[kOsc2Sync] || p[kOsc1FM] > 0 || (modEnv && p[kModEnvDest] == 1), moving = modEnv;
        for (int k = 0; k < 2; ++k) {
            const int dest = LfoDest(p, lfo, k);
            cross |= dest == 6;
            moving |= dest == 1 || dest == 2 || dest == 5;
        }
        using Chunk = void (Voice::*)(float *, float *, int, int64_t, int64_t, const PatchValues &, double,
                                      const double *const[2], const int32_t *const[2], double, double);
#define OXO_CHUNKS(osc, detune) \
    {&Voice::RenderChunkAs<osc, detune, VoiceFilter::kSvf>, &Voice::RenderChunkAs<osc, detune, VoiceFilter::kLadder>, \
     &Voice::RenderChunkAs<osc, detune, VoiceFilter::kDiode>}
        static constexpr Chunk kChunk[3][2][3] = {{OXO_CHUNKS(kOscStatic, false), OXO_CHUNKS(kOscStatic, true)},
                                                  {OXO_CHUNKS(kOscMoving, false), OXO_CHUNKS(kOscMoving, true)},
                                                  {OXO_CHUNKS(kOscCross, false), OXO_CHUNKS(kOscCross, true)}};
#undef OXO_CHUNKS
        (this->*kChunk[cross ? kOscCross : moving ? kOscMoving : kOscStatic][p[kOsc1Detune] > 0]
                      [VoiceFilter::KindOf(p[kFilterType])])(left, right, frames, pos, blockEnd, p, bendSemis, lfo,
                                                              lfoSteps, cutoffKnob, cutoffStep);
    }

    template <int kOsc, bool kDetune, int kFilter>
    void RenderChunkAs(float *left, float *right, int frames, int64_t pos, int64_t blockEnd, const PatchValues &p,
                       double bendSemis, const double *const lfo[2], const int32_t *const lfoSteps[2], double cutoffKnob,
                     double cutoffStep) {
        // ---- LFO routing (measured depth laws per destination). Both LFOs on
        // one destination (measured, measure_lfo_sum.py): pitch = sum of each
        // LFO's cents; cutoff / FM / pw / pan = the law applied to the
        // depth-weighted sum of the LFO values; amp = product of the gains.
        LfoRoute r[8];
        for (int k = 0; k < 2; ++k) {
            const int dest = LfoDest(p, lfo, k);
            if (!dest) continue;
            LfoRoute &route = r[dest];
            route.v[route.n] = lfo[k];
            route.steps[route.n] = lfoSteps[k];
            route.depth[route.n] = p[k ? kLfo2Depth : kLfo1Depth];
            route.scale[route.n] = route.depth[route.n] / 127.0;
            ++route.n;
        }
        // (measured) an LFO on pitch plays its own whole steps (Lfo.hpp), added
        // to the bend's truncated steps; both LFOs' steps add
        const auto pitchSteps = [&](const LfoRoute &route, int i) {
            int s = 0;
            for (int k = 0; k < route.n; ++k) s += route.steps[k][i];
            return s;
        };
        // ---- per-block parameters ----
        const double tuneSemis = tables::kFineCents[p[kOscFineTune]] / 100.0;
        // (measured) a glide plays in 1/128-octave steps: target + ceil(r / step)
        const double gap = glide_ - note_;
        const double played = note_ + (gap < 0 ? -1 : 1) * kGlideStep * std::ceil(std::abs(gap) / kGlideStep - 1e-9);
        // bend + LFO pitch: added per sample, in whole steps (PitchRatio)
        const double base = played + p[kOscKeyShift] + tuneSemis + unisonSemis_;
        const double f1 = NoteHz(base);
        const double osc2Note = (p[kOsc2KbdTrack] ? base : 57.0 + tuneSemis) +
                                tables::kOsc2PitchSemis[p[kOsc2Pitch]] + tables::kFineCents[p[kOsc2Fine]] / 100.0;
        const double f2 = NoteHz(osc2Note);
        const int t1 = int(std::lround(base + bendSemis)), t2 = int(std::lround(osc2Note + bendSemis));
        const int bendSteps = int(std::trunc(bendSemis * 100.0 / 9.375));
        const double bendRatio = std::exp2(9.375 * bendSteps / 1200.0);
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
        const bool ring = kOsc == kOscCross && p[kOsc2Ring] != 0, sync = kOsc == kOscCross && p[kOsc2Sync] != 0;
        const double fmBeta = FmBeta(p[kOsc1FM]);
        const int shape1 = p[kOsc1Shape], shape2 = p[kOsc2Shape];
        const double subGainKnob = p[kSubGain];
        const double subW = subGainKnob / (subGainKnob + 32.0), mainW = 1.0 - subW; // (measured) auto level
        const bool subOn = p[kSubGain] > 0;
        // (measured, measure_osc_mix.py) with the sub on, osc2 also takes the
        // main weight: osc1 (1-w)(1-m), sub w(1-m), osc2 (1-w)m, over 1 - wm
        const double mixNorm = 1.0 / (1.0 - mix2 * subW), mixA = mix1 * mixNorm, mixB = mix2 * mainW * mixNorm;
        const int subShape = p[kSubShape];
        const double subRatio = p[kSubOctave] ? 0.5 : 1.0;
        const bool detuneOn = kDetune;
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
        const bool modOn = kOsc != kOscStatic && p[kModEnvOn] != 0;
        const int modDest = p[kModEnvDest];
        // (measured) amount -> -1..+1: (amt - 64) / 63 above 64, / 64 below
        const double modAmt = (p[kModEnvAmount] - 64) / (p[kModEnvAmount] > 64 ? 63.0 : 64.0);

        const double ampGain = kOscAmplitude * tables::kAmpGain[p[kAmpGain]] *
                               std::pow(10.0, -15.0 * (p[kAmpVelSens] / 64.0) * (127 - velocity_) / 126.0 / 20.0);

        // (measured, measure_pw_limits.py) the final duty never reaches 0 / 1:
        // pw 0 = pw 1 = 1.25 / 254, modulation tops out at 1 - 1.375 / 254
        static constexpr double kWidthMin = 1.25 / 254.0, kWidthMax = 1.0 - 1.375 / 254.0;
        // a static oscillator section's steps and width (else per sample, below)
        const double staticIncA = PhaseStep(inc1 * bendRatio), staticIncB = inc2 * bendRatio;
        const double staticWidth = std::clamp(width, kWidthMin, kWidthMax);
        double curWidth = staticWidth;
        uint32_t curWidthPoints = CopyWidth(staticWidth);
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
        // the copies' tables (WaveTableSet::CopyTable): osc1's (a pulse = two
        // points of the saw) and the sub's
        using Copy = WaveTableSet::CopyShape;
        const float *const copy1 = shape1 == 0 ? tables_->CopySine()
                                               : tables_->CopyTable(shape1 == 3 ? Copy::kCopyTriangle : Copy::kCopySaw, std::clamp(t1, 0, 127));
        static constexpr Copy kSubCopy[4] = {Copy::kCopySaw, Copy::kCopyTriangle, Copy::kCopySaw, Copy::kCopySquare};
        const float *const copySub = (subShape & 3) == 0 ? tables_->CopySine()
                                                         : tables_->CopyTable(kSubCopy[subShape & 3], std::clamp(tSub, 0, 127));
        // the stored copy phases' step ratios (in Phase64 units / 2, see the phase update)
        const double copyRatio = copyHalf_ ? 0.5 : 1.0;
        const double copyScale = copyRatio * 0x1p63;
        // a copy's step <= |step| + incA * (2^(140 / 1200) - 1), incA < 0.5:
        // |step| below this stays under one cycle
        static constexpr double kFastStepLimit = 0.9;

        // (measured, key-synced square LFO: measure_lfo_keysync.py /
        // measure_lfo_cutoff.py) cutoff and amp are UNIPOLAR, u =
        // (lfo + 1) / 2 (linear in between assumed):
        // cutoff -> pulled toward knob 125 by (depth / 127) * u;
        // amp    -> gain 1 - (depth / 127) * (1 - u);
        // pw     -> (measured, measure_lfo_pw.py) the low-part duty moves toward
        //           1 (lfo > 0) or 0 (lfo < 0) by depth / 127 * |lfo| of the
        //           room left on that side: never wraps, +-0.5 for a square.
        // (pan: after the effect, Synth::Pan)

        // balance law (measured on unison spread): centre 1/1, hard side 1/0.
        // (measured, check_unison.py) the patch pan (Synth::Pan, after the
        // effect) MULTIPLIES its gains with the unison spread's.
        const double panL = std::min(1.0, 1.0 - unisonPan_), panR = std::min(1.0, 1.0 + unisonPan_);

        // ---- the oscillators' per-sample modulation, computed ahead for the
        // chunk: osc1 / osc2 steps, pulse width, FM index, each in its own
        // loop and only when something is routed to it. Kept out of the voice
        // loop below: there its branches and library calls (exp2, log) cost
        // ~100 instructions per sample even with nothing routed.
        double incAs[kMaxChunk], incBs[kMaxChunk], widths[kMaxChunk], fmBs[kMaxChunk];
        const Adsr modEnvBefore = modEnv_;
        if constexpr (kOsc != kOscStatic) {
            // (the mod env has one destination: the loop of that one advances it)
            const bool envPitch = modOn && modDest == 0, envFm = modOn && modDest == 1, envWidth = modOn && modDest == 2;
            if (r[1] || r[2] || envPitch) {
                PitchRatio ratioAll, ratio2;
                for (int i = 0; i < frames; ++i) {
                    const int lfoAll = r[2] ? pitchSteps(r[2], i) : 0;
                    const double mAll = r[2] ? ratioAll(bendSteps + lfoAll) : bendRatio;
                    const double m2 = r[1] ? ratio2(bendSteps + lfoAll + pitchSteps(r[1], i)) : mAll;
                    // (measured) mod env -> osc2 pitch: +-60 semitones at env 1 (not quantized)
                    const double mEnv2 = envPitch ? std::exp2(modEnv_.Next() * modAmt * 60.0 / 12.0) : 1.0;
                    incAs[i] = PhaseStep(inc1 * mAll), incBs[i] = inc2 * m2 * mEnv2;
                }
            } else {
                std::fill_n(incAs, frames, staticIncA);
                std::fill_n(incBs, frames, staticIncB);
            }
            if (r[5] || envWidth) {
                for (int i = 0; i < frames; ++i) {
                    double w = width;
                    if (r[5]) {
                        const double l = r[5].Signed(i);
                        w += l * (l > 0 ? 1.0 - width : width);
                    }
                    if (envWidth) {
                        // (measured, measure_modenv_pw.py) same room-left law
                        const double m = std::clamp(modEnv_.Next() * modAmt, -1.0, 1.0);
                        w += m * (m > 0 ? 1.0 - w : w);
                    }
                    widths[i] = std::clamp(w, kWidthMin, kWidthMax);
                }
            } else {
                std::fill_n(widths, frames, staticWidth);
            }
            if constexpr (kOsc == kOscCross) {
                if (r[6] || envFm) {
                    for (int i = 0; i < frames; ++i) {
                        // (measured, measure_fm_mod.py) in FM knob units (fractional):
                        // LFO unipolar toward 127 by depth / 127 * u; mod env
                        // bipolar room-left, m = (amt - 64) / 64 at env 1.
                        double knob = p[kOsc1FM];
                        if (r[6]) knob += (127.0 - knob) * r[6].Unipolar(i);
                        if (envFm) {
                            // the DECAY reaches FM in dB: 0 dB -> 1, -80 dB -> 0 (the
                            // offset falls LINEARLY in time, to 0 at 9.2 taus); the
                            // attack ramp is linear as elsewhere
                            const double e = (modEnv_.Next(), modEnv_.Level());
                            const double g = modEnv_.Attacking() ? e : e > 1e-4 ? 1.0 + std::log(e) / std::log(1e4) : 0.0;
                            const double m = std::clamp(modAmt * g, -1.0, 1.0);
                            knob += m * (m > 0 ? 127.0 - knob : knob);
                        }
                        const double k = std::clamp(knob, 0.0, 127.0);
                        const int j = std::min(int(k), 126);
                        fmBs[i] = FmBeta(j) + (FmBeta(j + 1) - FmBeta(j)) * (k - j);
                    }
                } else {
                    std::fill_n(fmBs, frames, fmBeta);
                }
            }
        }

        double fmIncA = NAN, fmRef = 0; // kFmRefInc / incA, recomputed when incA changes
        const double fmScale = shape2 == 4 ? kNoiseFm / kNoiseLevel : 1.0;
        // (measured, check_fm.py --ring) ring on, FM never applies, but with an
        // FM source in the patch the product takes osc2's pulse / noise at
        // full scale (x2 / x1 / 0.65039; saw and triangle unchanged): the FM
        // knob above 0, an LFO on FM (depth > 0, its trough too), a mod env on
        // FM with amount >= 67 all note long, at 65 / 66 only while its
        // offset is above 0 (9.2 taus into the decay)
        const bool ringFmSource = p[kOsc1FM] > 0 || r[6] || (modOn && modDest == 1 && p[kModEnvAmount] >= 67);
        const double ringFmGain = shape2 == 2 ? 2.0 : shape2 == 4 ? 1.0 / kNoiseLevel : 1.0;
        double copyStep = NAN, copyIncA = NAN; // the osc1 steps copyInc holds (the copies' steps, recomputed when they change)
        simd::P4 copyInc[2] = {};
        for (int i = 0; i < frames; ++i) {
            double incA = staticIncA, incB = staticIncB, w = staticWidth;
            if constexpr (kOsc != kOscStatic) {
                incA = incAs[i], incB = incBs[i], w = widths[i];
                curWidth = w;
                curWidthPoints = CopyWidth(w);
            }
            // ---- osc2 ----
            double o2 = 0;
            switch (shape2) {
            case 1: o2 = saw2.Read(osc2Phase_); break;
            case 2: o2 = 0.5 * (saw2.Read(osc2Phase_) - saw2.Read(WrapNext(osc2Phase_ + w))); break;
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
            double fm = 1.0;
            if constexpr (kOsc == kOscCross) {
                if (incA != fmIncA) fmIncA = incA, fmRef = kFmRefInc / incA * fmScale;
                if (!ring) fm = 1.0 + fmBs[i] * o2 * fmRef;
            }
            double o1 = OscRead(shape1, saw1, tri1, osc1Phase_, curWidth);
            double sub = subOn ? OscRead(subAs1, sawSub, triSub, subPhase_, 0.5) : 0.0;
            if (detuneOn) {
                const Phase64 *const ph = copyPhase_.data();
                const double acc = o1 + (shape1 == 2 ? 0.5f * (CopiesSum(copy1, ph, copyShift, 0) - CopiesSum(copy1, ph, copyShift, curWidthPoints))
                                                     : CopiesSum(copy1, ph, copyShift, 0));
                const double subAcc = subOn ? sub + CopiesSum(copySub, ph, 0, 0) : sub;
                // (measured, sync + FM: the copies' spread in Hz stays the
                // unmodulated one while sync's FM offset moves the pitch) every
                // copy takes osc1's FM deviation as it is, added to its own
                // detuned step, not scaled by its detune ratio.
                // A step under one cycle either way converts directly (two's
                // complement = mod 1)
                if (const double step = incA * fm; step != copyStep || incA != copyIncA) {
                    copyStep = step, copyIncA = incA;
                    const double deviation = step - incA;
                    Phase64 inc[kDetuneOscs];
                    if (std::fabs(step) < kFastStepLimit) {
                        for (int d = 0; d < kDetuneOscs; ++d)
                            inc[d] = Phase64(int64_t((incA * detuneRatio[d] + deviation) * copyScale)) << 1;
                    } else {
                        for (int d = 0; d < kDetuneOscs; ++d)
                            inc[d] = ToPhase64((incA * detuneRatio[d] + deviation) * copyRatio);
                    }
                    copyInc[0] = simd::Phases(inc[0], inc[1], inc[2], inc[3]);
                    copyInc[1] = simd::Phases(inc[4], inc[5], inc[6], inc[7]);
                }
                simd::StorePhases(copyPhase_.data(), simd::LoadPhases(copyPhase_.data()) + copyInc[0]);
                simd::StorePhases(copyPhase_.data() + 4, simd::LoadPhases(copyPhase_.data() + 4) + copyInc[1]);
                o1 = acc * detuneGain;
                // (measured, measure_ring_detune.py) ring on: the sub copies
                // skip the copy gain (~+10 dB). The original bounds that loud
                // sum AFTER the filter (Filter.hpp, the output limiter)
                sub = ring ? subAcc : subAcc * detuneGain;
            }
            if (osc1Silent) o1 = sub = 0;
            double osc1Out = subOn ? mainW * o1 + subW * sub : o1;
            if (ring) o2 *= ringFmSource || fmBs[i] > 0 ? o1 * ringFmGain : o1;

            if constexpr (kOsc == kOscCross) osc1Phase_ += PhaseStep(incA * fm);
            else osc1Phase_ += incA; // whole units already
            const bool forward = osc1Phase_ >= 1.0, backward = osc1Phase_ < 0.0;
            osc1Phase_ = Wrap(osc1Phase_);
            subPhase_ = Wrap(subPhase_ + incA * subRatio * fm);
            // (measured, sync + FM) the original syncs whenever osc1's phase
            // DROPS: a forward wrap, and every sample FM drives it backward
            // without wrapping (a backward wrap does not). Synced noise
            // restarts on a negative value, so once FM outweighs the pitch
            // the voice locks on a constant (Synth1's "FM above ~40 = a
            // click", threshold an octave of FM knob per octave of key)
            if (sync && (forward || (fm < 0 && !backward))) {
                // (measured, alias energy matches to 0.1 dB) the original's hard
                // sync is NAIVE: osc2 restarts at a fixed phase, no band-limiting
                osc2Phase_ = forward ? kSyncResetPhase : 0.0;
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
            double filterOut;
            if constexpr (kFilter == VoiceFilter::kSvf) {
                filter_.SetSvf(filterType, cutoff, filterRes);
                filterOut = filter_.ProcessSvf(mixed);
            } else if constexpr (kFilter == VoiceFilter::kLadder) {
                filter_.SetLadder(cutoff, filterRes);
                filterOut = filter_.ProcessLadder(mixed);
            } else {
                filter_.SetDiode(cutoff, filterRes);
                filterOut = filter_.ProcessDiode(mixed);
            }
            const double filtered = Saturate(filterOut, satA);
            const double env = AmpGain(amp_.Next(), pos + i, blockEnd);
            double gainMod = 1.0;
            for (int k = 0; k < r[4].n; ++k) gainMod *= 1.0 - r[4].scale[k] * 0.5 * (1.0 - r[4].v[k][i]);
            const double x = filtered * env * ampGain * gainMod;
            const double y = x; // (the original's 22 kHz output lowpass = the EQ tone stage)
            left[i] += float(y * panL);
            right[i] += float(y * panR);
            if (!amp_.Active() && (((pos + i + 1) & 255) == 0 || pos + i + 1 >= blockEnd)) {
                tail_ = false;
                // the voice ends here: the mod env stops with it (it ran ahead)
                if constexpr (kOsc != kOscStatic) {
                    if (modOn) {
                        modEnv_ = modEnvBefore;
                        for (int k = 0; k <= i; ++k) modEnv_.Next();
                    }
                }
                break;
            }
            tail_ = !amp_.Active();
        }
    }

    // (measured: a locked synced-noise DC through the amp env, whole
    // seconds) the original's amp env outside the attack is a CONTROL-RATE
    // gain: at every 256-sample boundary of the host block (the voice end
    // grid) it takes the envelope's value floored to 1/2048 (11 bits: the
    // sustain table's grid; 1/2048 = -66 dB, where decays and releases
    // land), and ramps linearly to the next boundary's. The attack stays per
    // sample, and so is the first block after a note on (the original
    // updates every ~1 ms there: kinks at +64, +107, +193, +255 samples from
    // the note's sub-block); the ramps start at the first grid boundary
    // 256 samples or more after it. A stage change mid-ramp restarts the
    // ramp from the current gain. `at` = the sample's offset in the host block.
    OXO_INLINE double AmpGain(double env, int64_t at, int64_t blockEnd) {
        const Adsr::Stage stage = amp_.CurrentStage();
        const bool boundary = (at & 255) == 0;
        if (ampSince_ < 256) ++ampSince_;
        if (stage == Adsr::Stage::Attack || stage == Adsr::Stage::Idle || (ampLeft_ <= 0 && (ampSince_ < 256 || !boundary))) {
            ampStage_ = stage;
            ampLeft_ = 0;
            return ampGain_ = env;
        }
        if (boundary || stage != ampStage_ || ampLeft_ <= 0) {
            if (boundary) ampGain_ = std::floor(env * 2048.0) * (1.0 / 2048);
            ampStage_ = stage;
            const int64_t n = std::max<int64_t>(1, std::min<int64_t>(256 - (at & 255), blockEnd - at));
            Adsr ahead = amp_;
            for (int64_t k = 0; k < n; ++k) ahead.Next();
            ampStep_ = (std::floor(ahead.Level() * 2048.0) * (1.0 / 2048) - ampGain_) / double(n);
            ampLeft_ = int(n);
            return ampGain_;
        }
        --ampLeft_;
        return ampGain_ += ampStep_;
    }

    // osc1's shape (0 sine, 1 saw, 2 pulse of duty `width`, 3 tri) at phase ph
    static OXO_INLINE double OscRead(int shape, const WaveTable &saw, const WaveTable &tri, double ph, double width) {
        switch (shape) {
        case 0: return Sin2Pi(ph);
        case 1: return saw.Read(ph);
        case 2: return 0.5 * (saw.Read(ph) - saw.Read(WrapNext(ph + width)));
        default: return tri.Read(ph);
        }
    }
    // the 8 detune copies' sum: the table's points at the phases ph << shift,
    // truncated (no interpolation, see WaveTableSet::CopyTable), `offset`
    // points further (a pulse's second saw)
    static OXO_INLINE float CopiesSum(const float *table, const Phase64 *ph, int shift, uint32_t offset) {
        float v[kDetuneOscs];
        for (int d = 0; d < kDetuneOscs; ++d)
            v[d] = table[(uint32_t((ph[d] << shift) >> (64 - WaveTableSet::kCopyBits)) + offset) & (WaveTableSet::kCopyPoints - 1)];
        return ((v[0] + v[1]) + (v[2] + v[3])) + ((v[4] + v[5]) + (v[6] + v[7]));
    }
    // a pulse width in whole table points: (measured) the copies' pulse is
    // two points of the saw a whole number of points apart (the harmonics a
    // width cancels leave no lines either, e.g. the 4th and 8th at width 64)
    static OXO_INLINE uint32_t CopyWidth(double width) { return uint32_t(width * WaveTableSet::kCopyPoints); }

    // The LFOs (0..2) routed to one destination.
    struct LfoRoute {
        const double *v[2] = {nullptr, nullptr};
        const int32_t *steps[2] = {nullptr, nullptr}; // pitch steps (pitch routes only)
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
    // the same for ph in [0, 2) (a phase + a pulse width), never a call
    static OXO_INLINE double WrapNext(double ph) { return ph >= 1.0 ? ph - 1.0 : ph; }
    // osc1's detune copy d (the sub's copy d = copyPhase_[d])
    Phase64 CopyPhase(int d) const { return copyHalf_ ? copyPhase_[d] << 1 : copyPhase_[d]; }
    // osc1's copy phases, the sub's locked to them (half = sub octave)
    void SetCopyPhases(const Phase64 *osc1Copies, bool half) {
        copyHalf_ = half;
        for (int d = 0; d < kDetuneOscs; ++d) copyPhase_[d] = half ? osc1Copies[d] >> 1 : osc1Copies[d];
    }
    // FM index per knob value. (measured: osc1 sine, osc2 a fifth up, mix on
    // osc1) FM 0 = no FM at all, sidebands at the -178 dB floor: the table's
    // entry 0 (6.6e-6, sidebands at -115 dB) is fit noise.
    static OXO_INLINE double FmBeta(int knob) { return knob > 0 ? double(tables::kFmBeta[knob]) : 0.0; }
    // (measured, measure_porta.py) a glide aims 1.35 % of its starting
    // distance past the target (the curve scales with the interval)
    static constexpr double kGlideStep = 12.0 / 128, kGlideAim = 0.0135; // semitones, x distance
    // (measured, measure_pitch_mod.py, measure_lfo_pitch.py) pitch bend and
    // LFO pitch play in 1/128-octave steps (9.375 cents): the bend truncated
    // toward 0, each LFO its own steps (Lfo.hpp), added (the mod env's pitch
    // is not quantized). exp2(steps * 9.375 / 1200), recomputed only when
    // the step count changes.
    struct PitchRatio {
        int steps = INT32_MIN;
        double ratio = 1;
        OXO_INLINE double operator()(int s) {
            if (s != steps) steps = s, ratio = std::exp2(9.375 * s / 1200.0);
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
    double unisonSemis_ = 0, unisonPan_ = 0, unisonShift_ = 0;
    int unisonIndex_ = 0;
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
    double ampGain_ = 0, ampStep_ = 0; // the control-rate amp gain (AmpGain)
    int ampLeft_ = 0, ampSince_ = 0;
    Adsr::Stage ampStage_ = Adsr::Stage::Idle;
};

} // namespace oxo::s2
