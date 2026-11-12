#pragma once

// Synth1 LFO (measured): global, free running from instance creation (key
// sync restarts it on the first note of a phrase), bipolar +-1.
// Native types: 0 saw (falling, +1 at phase 0), 1 triangle (-1 at phase 0),
// 2 square (+1 first half), 3 sample & hold, 4 smooth random, 5 sine.
// Speed: Hz = 2^(speed / 12.0367) / 12.
//
// (measured, measure_lfo_pitch.py) ON PITCH an LFO plays whole 1/128-octave
// steps of its own, N = PitchSteps(depth) at full swing, the step law
// depending on the waveform (v = the LFO value, -1..1):
//   saw       2N + 1 equal bands of v: floor((v + 1) / 2 * (2N + 1)) - N
//   triangle  rising floor(N v), falling ceil(N v): a step is taken when
//             the wave reaches it (each step lags by up to one)
//   square    +-N
//   S&H, smooth random  floor(N v) (-N .. N - 1)
//   sine      sign(v) floor(N |v| + 1/4)
// Same at every speed; the bend's (truncated) steps and both LFOs' steps add.

#include "MeasuredTables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oxo::s2 {

// (measured, measure_lfo_random_keysync.py) the random types share ONE value
// per cycle, r: S&H plays r, smooth random ramps LINEARLY from the previous
// cycle's r to r (continuous: it reaches r at the cycle's end). On AMP only it
// ramps from -r to minus the previous r instead (it jumps at every cycle
// start). A key-sync restart draws a new r as a cycle start does: every
// restarted note gets a new value (smooth: ramps from the last r toward it).
class Lfo {
public:
    Lfo() { Reset(); }
    void Reset() {
        phase_ = 0;
        state_ = kSeed;
        prev_ = Random();
        cur_ = Random();
    }
    void Restart() {
        phase_ = 0;
        Next();
    }

    // (measured, measure_lfo_speed.py: square LFO flip times, 0.02 %) speed 0
    // = a 12 s cycle
    static double SpeedHz(int speed) { return std::exp2(speed / 12.0367) / 12.0; }

    // (measured, measure_lfo_sync.py: every knob value) tempo sync: 30 steps,
    // step = floor(speed * 30 / 128), LFO cycle in beats (quarter notes),
    // 16 bars at 0 ... 1/64T at 127 (= the labels on Synth1's own display).
    static constexpr int kSyncSteps = 30;
    static constexpr double kSyncBeats[kSyncSteps] = {
        64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3.5, 3, 2, 1.75, 1.5, 4.0 / 3, 1, 0.875, 0.75, 2.0 / 3, 0.5,
        0.375, 1.0 / 3, 0.25, 1.0 / 6, 0.125, 1.0 / 12, 1.0 / 24};
    static int SyncStep(int speed) { return std::clamp(speed, 0, 127) * kSyncSteps / 128; }
    static double SyncBeats(int speed) { return kSyncBeats[SyncStep(speed)]; }

    // Pitch steps at full swing per depth (the measured cents table, whole
    // steps: a square LFO shows them).
    static int PitchSteps(int depth) { return int(tables::kLfoPitchCents[std::clamp(depth, 0, 127)] / 9.375 + 0.5); }

    // Fills out[0..n) with the waveform at `hz`; steps (when not null) with
    // its pitch steps for `pitchSteps` = PitchSteps(depth) (see above); `amp`
    // = routed to amp (smooth random's own form there).
    void Render(double *out, int n, int type, double hz, double sampleRate, int32_t *steps = nullptr, int pitchSteps = 0,
                bool amp = false) {
        const double inc = hz / sampleRate, sn = pitchSteps;
        for (int i = 0; i < n; ++i) {
            switch (type) {
            case 0: out[i] = 1.0 - 2.0 * phase_; break;
            case 1: out[i] = phase_ < 0.5 ? -1.0 + 4.0 * phase_ : 3.0 - 4.0 * phase_; break;
            case 2: out[i] = phase_ < 0.5 ? 1.0 : -1.0; break;
            case 3: out[i] = cur_; break;
            case 4: out[i] = amp ? -(cur_ + (prev_ - cur_) * phase_) : prev_ + (cur_ - prev_) * phase_; break;
            default: out[i] = std::sin(2.0 * M_PI * phase_); break;
            }
            if (steps) {
                const double v = out[i];
                double s;
                switch (type) {
                case 0: s = std::min(std::floor((v + 1.0) * 0.5 * (2 * sn + 1)) - sn, sn); break;
                case 1: s = phase_ < 0.5 ? std::floor(sn * v) : std::ceil(sn * v); break;
                case 2: s = v > 0 ? sn : -sn; break;
                case 3:
                case 4: s = std::floor(sn * v); break;
                default: s = std::copysign(std::floor(sn * std::fabs(v) + 0.25), v); break;
                }
                steps[i] = int32_t(s);
            }
            phase_ += inc;
            if (phase_ >= 1.0) {
                phase_ -= std::floor(phase_);
                Next();
            }
        }
    }

private:
    static constexpr uint32_t kSeed = 0x2545F491u;

    void Next() {
        prev_ = cur_;
        cur_ = Random();
    }
    double Random() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_ * (2.0 / 4294967296.0) - 1.0;
    }

    double phase_ = 0, prev_ = 0, cur_ = 0;
    uint32_t state_ = kSeed;
};

} // namespace oxo::s2
