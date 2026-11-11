#pragma once

// Global effects after the voice sum: tempo delay, then chorus/flanger.
//
// Delay (measured): time = tempo division table (kDelayBeats), feedback =
// fb / 127 per repeat, dry/wet crossfade (64 = both at unity), spread
// delays ONE side by up to +100 ms (< 64 right, > 64 left, exponential), types 0 ST
// (independent), 1 X (cross: each input enters the OTHER side's line, the lines
// feed each other: a left input echoes right, left, right...), 2 PP
// (ping-pong, input -> left).
// Whole samples (measured, single-tap impulse responses at 44.1 / 96 kHz,
// tempos 60..200): the division rounds DOWN to a multiple of 4 samples,
// the spread's offset down to a sample, at least 0.1 ms (floored): time 0
// = 4 samples at 44.1 kHz, a comb, not 29. A new time GLIDES there
// exponentially, tau 0.215 s (tape-like), reads interpolated meanwhile.
// Tone (measured, measure_delay_tone.py): a bilinear one-pole in the echo
// path, 64 = no filter, below = lowpass (200 Hz .. 22 kHz at 63), above =
// highpass (10 Hz .. 8 kHz): kDelayToneHz.
//
// Chorus: measured, see the class comment.

#include "MeasuredTables.hpp"
#include "Params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace oxo::s2 {

inline constexpr double kDelayBeats[20] = {0.0,    1.0 / 24, 1.0 / 12, 1.0 / 8, 1.0 / 6, 0.25, 1.0 / 3,
                                           0.375,  0.5,      2.0 / 3,  0.75,    0.875,   1.0,  4.0 / 3,
                                           1.5,    1.75,     2.0,      3.0,     3.5,     4.0};

// A delay line index i < 2 * size back into [0, size) (a compare: `% size`
// is a 64-bit division, tens of cycles each on older x86).
inline size_t WrapIndex(size_t i, size_t size) { return i >= size ? i - size : i; }

// Bilinear one-pole (analog cutoff, NO prewarp): the EQ / delay tone
// (measured, measure_eq_tone.py, kEqToneHz).
class BilinearOnePole {
public:
    void Set(double hz, bool highpass, double sr) {
        const double k = M_PI * hz / sr;
        hp_ = highpass;
        bypass_ = false;
        g_ = (highpass ? 1.0 : k) / (1 + k);
        b_ = (1 - k) / (1 + k);
    }
    void SetBypass() { bypass_ = true; }
    double Process(double x) {
        if (bypass_) return x;
        const double y = g_ * (hp_ ? x - x1_ : x + x1_) + b_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }
    void Reset() { x1_ = y1_ = 0; }

private:
    bool hp_ = false, bypass_ = false;
    double g_ = 1, b_ = 0, x1_ = 0, y1_ = 0;
};

class TempoDelay {
public:
    void Prepare(double sr) {
        sr_ = sr;
        const size_t n = size_t(sr * 9.0) + 4; // 4 beats at 30 bpm x 1.2 spread fits
        buf_[0].assign(n, 0.0f);
        buf_[1].assign(n, 0.0f);
        pos_ = 0;
        smoothL_ = smoothR_ = -1;
        fadeLen_ = std::max(1, int(sr * kClearFadeSeconds));
        glide_ = 1.0 - std::exp(-1.0 / (kGlideSeconds * sr));
        fadeOut_ = 0;
        Empty();
    }
    void Reset() {
        fadeOut_ = 0;
        Empty();
    }
    // A patch change: the echoes fade out, then the line empties and its
    // input fades back in (no click either way), so the switch's transient
    // never rings in a long feedback. Repeated calls during the fade keep it.
    void Clear() {
        if (!fadeOut_) fadeOut_ = fadeLen_;
    }
    // Delay off: nothing to hear, a pending clear empties at once.
    void ClearNow() {
        if (fadeOut_) fadeOut_ = 0, Empty(), inGain_ = 0;
    }

    // wetL / wetR given: l / r keep the dry part, the echoes go there (the
    // chorus takes them, see Chorus)
    void Process(float *l, float *r, int n, const PatchValues &p, double tempo, float *wetL = nullptr, float *wetR = nullptr) {
        const double beat = 60.0 / std::max(tempo, 20.0);
        const double base = 4.0 * std::floor(kDelayBeats[std::clamp<int>(p[kDelayTime], 0, 19)] * beat * sr_ / 4.0);
        // (measured, reference/meta/delay_spread.json, every value) one side
        // later by 100 ms * 10^((|v - 64| - 63) / 21): 0 at 64, 1 ms at 85 / 43
        const int sd = std::min(std::abs(p[kDelaySpread] - 64), 63);
        const double s = p[kDelaySpread] - 64;
        const double offset = sd ? std::floor(0.1 * sr_ * std::pow(10.0, (sd - 63) / 21.0)) : 0.0;
        const double minT = std::floor(1e-4 * sr_);
        double tl = std::max(minT, base + (s > 0 ? offset : 0)), tr = std::max(minT, base + (s < 0 ? offset : 0));
        const double maxT = double(buf_[0].size() - 4);
        tl = std::min(tl, maxT);
        tr = std::min(tr, maxT);
        if (smoothL_ < 0) smoothL_ = tl, smoothR_ = tr;
        const double fb = p[kDelayFeedback] / 127.0;
        const double dw = p[kDelayDryWet];
        // (measured, IR taps) dry = (127 - dw) / 63 above 64, unity below
        const double wet = std::min(1.0, dw / 64.5), dry = std::min(1.0, (127 - dw) / 63.0);
        const int type = p[kDelayType];
        const int t = p[kDelayTone];
        for (auto &f : tone_) {
            if (t == 64) f.SetBypass();
            else f.Set(tables::kDelayToneHz[std::clamp(t, 0, 127)], t > 64, sr_);
        }
        const size_t size = buf_[0].size();
        const double fadeStep = 1.0 / fadeLen_;
        for (int i = 0; i < n; ++i) {
            double out = 1;
            if (fadeOut_ > 0) {
                if (--fadeOut_ == 0) Empty(), inGain_ = 0, smoothL_ = tl, smoothR_ = tr; // the new patch's time, no glide
                else out = fadeOut_ * fadeStep;
            }
            // tape-like glide toward a new time
            smoothL_ += (tl - smoothL_) * glide_;
            smoothR_ += (tr - smoothR_) * glide_;
            const double el = tone_[0].Process(Read(0, smoothL_)), er = tone_[1].Process(Read(1, smoothR_));
            const double dryL = l[i], dryR = r[i], inL = dryL * inGain_, inR = dryR * inGain_;
            if (inGain_ < 1) inGain_ = std::min(1.0, inGain_ + fadeStep);
            double wl, wr;
            switch (type) {
            case 1: wl = inR + fb * er, wr = inL + fb * el; break; // (measured: a left input echoes right first)
            case 2: wl = 0.5 * (inL + inR) + fb * er, wr = el; break;
            default: wl = inL + fb * el, wr = inR + fb * er; break;
            }
            buf_[0][pos_] = float(wl);
            buf_[1][pos_] = float(wr);
            if (++pos_ == size) pos_ = 0;
            if (written_ < size) ++written_;
            if (wetL) {
                l[i] = float(dry * dryL), r[i] = float(dry * dryR);
                wetL[i] = float(wet * out * el), wetR[i] = float(wet * out * er);
            } else {
                l[i] = float(dry * dryL + wet * out * el);
                r[i] = float(dry * dryR + wet * out * er);
            }
        }
    }

private:
    static constexpr double kClearFadeSeconds = 0.015;
    static constexpr double kGlideSeconds = 0.215; // (measured) time-change glide tau

    // O(1) (no multi-MB fill on the audio thread): samples older than the
    // last Empty() read as silence.
    void Empty() {
        written_ = 0;
        inGain_ = 1;
        tone_[0].Reset();
        tone_[1].Reset();
    }
    float At(int ch, size_t i) const {
        const size_t size = buf_[ch].size();
        return written_ >= size || WrapIndex(pos_ + size - 1 - i, size) < written_ ? buf_[ch][i] : 0.0f;
    }
    double Read(int ch, double delay) const {
        const size_t size = buf_[ch].size();
        double rp = double(pos_) - delay;
        while (rp < 0) rp += double(size);
        const size_t whole = size_t(rp), i0 = WrapIndex(whole, size), i1 = WrapIndex(i0 + 1, size);
        const double f = rp - double(whole);
        return At(ch, i0) * (1 - f) + At(ch, i1) * f;
    }

    double sr_ = 44100;
    std::vector<float> buf_[2];
    size_t pos_ = 0, written_ = 0;
    double smoothL_ = -1, smoothR_ = -1, glide_ = 0.0001;
    int fadeLen_ = 1, fadeOut_ = 0;
    double inGain_ = 1;
    BilinearOnePole tone_[2];
};

// Chorus (measured, measure_chorus.py: identical-noise IRs + sine phase
// demodulation): per-channel delay lines; voice delay = base * (1 + D *
// sin(w t - theta)), D = (100^(depth / 127) - 1) / 99 (depth 127 swings
// 0 .. 2 x base), one sine LFO free-running from creation at 2.94 Hz *
// 2^((rate - 64) / 8.89) (rate 0 = 0.01 Hz). Types 1-3: L -> L and R -> R, one voice each (R
// voice theta 0 for type 1, 90 degrees for types 2 / 3 = identical). Type
// 4: L -> L 0, R -> L 90, L -> R 180, R -> R 270 degrees. Comb feedback
// into each channel's own line, 0.99 * (fb - 64) / 64; wet = level / 127,
// dry stays at unity. With the DELAY on (measured, IR taps): its echoes
// never reach the output directly, they go through the chorus voices only,
// at unity whatever the level (level 0 = the echoes come chorus-delayed):
// out = dry + level * chorus(dry) + chorus(echoes), feedback alike.
class Chorus {
public:
    void Prepare(double sr) {
        sr_ = sr;
        for (auto &b : buf_) b.assign(size_t(sr * 0.08) + 4, 0.0f);
        pos_ = 0;
    }
    void Reset() {
        for (auto &b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        echoes_ = false;
    }

    // (measured, single-tap impulse responses at depth 0) wet base delay
    // per time knob in WHOLE samples at 44.1 kHz (30 ms at 127, the linear
    // half from 64); 96 kHz is the same delays rounded to its samples
    static double TimeSamples(int v, double sr) {
        static constexpr short t[128] = {
            2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 17, 19, 22, 24,
            26, 29, 32, 35, 38, 41, 45, 49, 53, 57, 62, 67, 72, 77, 83, 90,
            96, 103, 111, 119, 127, 136, 145, 155, 166, 177, 188, 201, 214, 228, 242, 257,
            273, 290, 308, 327, 347, 368, 389, 412, 437, 462, 489, 517, 546, 577, 609, 643,
            666, 677, 687, 697, 708, 718, 729, 739, 750, 760, 770, 781, 791, 802, 812, 822,
            833, 843, 854, 864, 875, 885, 895, 906, 916, 927, 937, 947, 958, 968, 979, 989,
            1000, 1010, 1020, 1031, 1041, 1052, 1062, 1072, 1083, 1093, 1104, 1114, 1125, 1135, 1145, 1156,
            1166, 1177, 1187, 1197, 1208, 1218, 1229, 1239, 1250, 1260, 1270, 1281, 1291, 1302, 1312, 1323,
        };
        return std::round(t[std::clamp(v, 0, 127)] * sr / 44100.0);
    }

    // (measured: 130 s renders) knob 0 is 0.01 Hz (the manual's bottom), not
    // the law's 0.020; knob 1 = 0.0216 follows it
    static double RateHz(int v) { return v <= 0 ? 0.01 : 2.9409 * std::exp2((v - 64) / 8.89); }
    // (fitted on sideband spectra) the swing shrinks at audio rates:
    // x0.98 at 36 Hz, x0.92 at 124 Hz, x0.62 at 400 Hz (log-f interpolated)
    static double RateSwing(double hz) {
        static constexpr double f[4] = {10, 36, 124, 400}, a[4] = {1.0, 0.98, 0.92, 0.62};
        if (hz <= f[0]) return 1.0;
        int i = 0;
        while (i < 2 && hz > f[i + 1]) ++i;
        const double t = std::clamp(std::log(hz / f[i]) / std::log(f[i + 1] / f[i]), 0.0, 1.0);
        return a[i] + (a[i + 1] - a[i]) * t;
    }
    static double Depth(int v) { return (std::pow(100.0, v / 127.0) - 1.0) / 99.0; }

    // echoL / echoR: the delay's echoes (nullptr: delay off)
    void Process(float *l, float *r, int n, const PatchValues &p, const float *echoL = nullptr, const float *echoR = nullptr) {
        const bool quad = p[kChorusType] >= 4;
        const double thetaR = p[kChorusType] == 1 ? 0.0 : 0.25; // cycles
        const double base = TimeSamples(p[kChorusTime], sr_);
        const double depth = base * Depth(p[kChorusDepth]) * RateSwing(RateHz(p[kChorusRate]));
        // 0.99: the manual's -99 % / +97 % labels; echo decay vs ours (same
        // probe) was 1-3 % stronger with 0.98
        const double fb = 0.99 * (p[kChorusFeedback] - 64) / 64.0;
        const double level = p[kChorusLevel] / 127.0;
        const double inc = RateHz(p[kChorusRate]) / sr_;
        const double maxD = double(buf_[0].size() - 3);
        if (!echoL && echoes_) { // the delay went off: its chorus lines empty
            std::fill(buf_[2].begin(), buf_[2].end(), 0.0f);
            std::fill(buf_[3].begin(), buf_[3].end(), 0.0f);
            echoes_ = false;
        }
        echoes_ = echoL != nullptr;
        const size_t size = buf_[0].size();
        for (int i = 0; i < n; ++i) {
            double d[4]; // tap delays: L voice, R voice (types 1-3) / LL RL LR RR (type 4)
            const auto at = [&](double theta) { return std::clamp(base + depth * std::sin(2 * M_PI * (lfoPhase_ - theta)), 0.0, maxD); };
            if (quad) d[0] = at(0.0), d[1] = at(0.25), d[2] = at(0.5), d[3] = at(0.75);
            else d[0] = at(0.0), d[1] = at(thetaR);
            // write first (delays down to 0 read the current input); the comb
            // adds the tap read now (measured: echoes every base delay, no
            // extra sample)
            const auto voices = [&](int line, double inL, double inR, double &wetL, double &wetR) {
                buf_[line][pos_] = float(inL);
                buf_[line + 1][pos_] = float(inR);
                double fbl, fbr;
                if (quad) {
                    const double ll = Read(line, d[0]), rl = Read(line + 1, d[1]), lr = Read(line, d[2]), rr = Read(line + 1, d[3]);
                    wetL = ll + rl, wetR = lr + rr, fbl = ll, fbr = rr;
                } else {
                    wetL = fbl = Read(line, d[0]);
                    wetR = fbr = Read(line + 1, d[1]);
                }
                buf_[line][pos_] = float(inL + fb * fbl);
                buf_[line + 1][pos_] = float(inR + fb * fbr);
            };
            double wetL, wetR;
            voices(0, l[i], r[i], wetL, wetR);
            double outL = l[i] + level * wetL, outR = r[i] + level * wetR;
            if (echoL) {
                voices(2, echoL[i], echoR[i], wetL, wetR);
                outL += wetL, outR += wetR;
            }
            if (++pos_ == size) pos_ = 0;
            lfoPhase_ += inc;
            if (lfoPhase_ >= 1.0) lfoPhase_ -= std::floor(lfoPhase_);
            l[i] = float(outL);
            r[i] = float(outR);
        }
    }

    // LFO runs while the chorus is off too (free running from creation).
    void Advance(int n, const PatchValues &p) {
        lfoPhase_ += RateHz(p[kChorusRate]) / sr_ * n;
        lfoPhase_ -= std::floor(lfoPhase_);
    }

private:
    // delay in samples; 0 = the sample just written.
    double Read(int line, double delay) const {
        const std::vector<float> &b = buf_[line];
        const size_t size = b.size();
        double rp = double(pos_) - delay;
        while (rp < 0) rp += double(size);
        const size_t whole = size_t(rp), i0 = WrapIndex(whole, size), i1 = WrapIndex(i0 + 1, size);
        const double f = rp - double(whole);
        return b[i0] * (1 - f) + b[i1] * f;
    }

    double sr_ = 44100;
    std::vector<float> buf_[4]; // L R lines, then the delay echoes' L R
    size_t pos_ = 0;
    double lfoPhase_ = 0;
    bool echoes_ = false;
};

} // namespace oxo::s2

namespace oxo::s2 {

// Peaking biquad matched to the analog prototype
//   H(s) = (s^2 + s G w0 / Q + w0^2) / (s^2 + s w0 / Q + w0^2)
// (G = centre gain, Q = the poles' Q; M. Vicanek, "Matched Second Order
// Digital Filters", 2016: no bilinear cramping near Nyquist), stereo.
class PeakEq {
public:
    // poleQ: the poles' Q; the zeros' Q = poleQ / gain (gain at f0 = gain).
    // (measured, measure_eq.py) poles AND zeros mapped by impulse invariance
    // (z = exp(s T) per root pair), the gain set at f0: the original's DC gain
    // falls below 1 for a boost near the top (-2.3 dB at 16 kHz).
    void Set(double hz, double poleQ, double gain, double sr) {
        const double w0 = 2 * M_PI * std::min(hz, sr * 0.49) / sr;
        const auto pair = [w0](double q, double &c1, double &c2) {
            const double zeta = 1 / (2 * q), e = std::exp(-zeta * w0);
            c1 = zeta <= 1 ? -2 * e * std::cos(std::sqrt(1 - zeta * zeta) * w0)
                           : -2 * e * std::cosh(std::sqrt(zeta * zeta - 1) * w0);
            c2 = e * e;
        };
        double z1, z2;
        pair(poleQ, a1_, a2_);
        pair(poleQ / gain, z1, z2);
        const std::complex<double> u = std::polar(1.0, -w0);
        const double k = gain * std::abs(1.0 + a1_ * u + a2_ * u * u) / std::abs(1.0 + z1 * u + z2 * u * u);
        b0_ = k, b1_ = k * z1, b2_ = k * z2;
    }
    double Process(int ch, double x) {
        double *z = z_[ch];
        const double y = b0_ * x + z[0];
        z[0] = b1_ * x - a1_ * y + z[1];
        z[1] = b2_ * x - a2_ * y;
        return y;
    }
    void Reset() { z_[0][0] = z_[0][1] = z_[1][0] = z_[1][1] = 0; }

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z_[2][2] = {{0, 0}, {0, 0}};
};

// Equalizer. Peak (measured, measure_eq.py / check_eq.py, within 0.1 dB at
// every knob): f0 = 50 Hz * 320^(freq / 127) (the v1.12 manual's 50 Hz..16
// kHz), gain (level - 64) * 50 / 127 dB (-25.2..+24.8), analog prototype with
// the poles' Q = 3.18 * 2^((q - 64) / 38.16) for a boost and that Q * G for a
// cut (= the exact inverse of the boost), discretized by impulse invariance
// (PeakEq).
// Tone (measured, measure_eq_tone.py): ONE bilinear one-pole (analog cutoff
// kEqToneHz, no prewarp), always on: <= 64 a lowpass (64 = 22 kHz: the
// original's "fixed output lowpass" is this stage), > 64 a highpass (no
// lowpass left).
class Equalizer {
public:
    void Prepare(double sr) { sr_ = sr; }
    void Reset() {
        peak_.Reset();
        tone_[0].Reset();
        tone_[1].Reset();
    }
    void Process(float *l, float *r, int n, const PatchValues &p) {
        const bool peakOn = p[kEqLevel] != 64;
        const double gain = std::pow(10.0, (p[kEqLevel] - 64) * 50.0 / 127.0 / 20.0);
        const double q = 3.18 * std::exp2((p[kEqQ] - 64) / 38.16);
        peak_.Set(50.0 * std::pow(320.0, p[kEqFreq] / 127.0), gain < 1 ? q * gain : q, gain, sr_);
        const int t = std::clamp(p[kEqTone], 0, 127);
        for (auto &f : tone_) f.Set(tables::kEqToneHz[t], t > 64, sr_);
        for (int i = 0; i < n; ++i) {
            double a = l[i], b = r[i];
            if (peakOn) a = peak_.Process(0, a), b = peak_.Process(1, b);
            l[i] = float(tone_[0].Process(a));
            r[i] = float(tone_[1].Process(b));
        }
    }

private:
    double sr_ = 44100;
    PeakEq peak_;
    BilinearOnePole tone_[2];
};

// RBJ biquad (bilinear, prewarped), stereo state.
class Biquad {
public:
    void Set(double hz, double q, bool highpass, double sr) {
        const double w = 2 * M_PI * std::min(hz, sr * 0.49) / sr, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        const double k = highpass ? (1 + c) / 2 : (1 - c) / 2;
        b0_ = k / a0, b1_ = (highpass ? -2 * k : 2 * k) / a0, b2_ = k / a0, a1_ = -2 * c / a0, a2_ = (1 - al) / a0;
    }
    double Process(int ch, double x) {
        double *z = z_[ch];
        const double y = b0_ * x + z[0];
        z[0] = b1_ * x - a1_ * y + z[1];
        z[1] = b2_ * x - a2_ * y;
        return y;
    }
    void Reset() { z_[0][0] = z_[0][1] = z_[1][0] = z_[1][1] = 0; }

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z_[2][2] = {{0, 0}, {0, 0}};
};

// a.d.1 curve f(w) = w / (1 + |w / L|^k)^(1/k), L / k per side, tabulated
// (value + slope, cubic Hermite) on u = w / (|w| + 1) in [-1, 1]: max error
// -114 dB, slope error 3e-6 (the exact curve's pows were the costliest part
// of the effect).
struct Ad1Curve {
    static constexpr int kSize = 1024;
    double t[kSize + 2][2]; // value, slope per table step
    Ad1Curve(double posLimit, double posKnee, double negLimit, double negKnee) {
        for (int i = 1; i < kSize; ++i) {
            const double u = 2.0 * i / kSize - 1.0, w = u / (1.0 - std::fabs(u));
            const double lim = u >= 0 ? posLimit : negLimit, knee = u >= 0 ? posKnee : negKnee;
            const double a = 1.0 + std::pow(std::fabs(w / lim), knee);
            t[i][0] = w / std::pow(a, 1.0 / knee);
            t[i][1] = std::pow(a, -1.0 / knee - 1.0) * (2.0 / kSize) / ((1.0 - std::fabs(u)) * (1.0 - std::fabs(u)));
        }
        t[0][0] = -negLimit, t[kSize][0] = t[kSize + 1][0] = posLimit;
        t[0][1] = t[kSize][1] = t[kSize + 1][1] = 0.0;
    }
    double operator()(double w) const {
        const double x = (w / (std::fabs(w) + 1.0) + 1.0) * (0.5 * kSize);
        const int i = int(x);
        const double f = x - i, y0 = t[i][0], m0 = t[i][1], m1 = t[i + 1][1], d = t[i + 1][0] - y0;
        return y0 + f * (m0 + f * (3.0 * d - 2.0 * m0 - m1 + f * (m0 + m1 - 2.0 * d)));
    }
};
// (fit_ad1.py) the first curve (inside the loop) and the second
inline const Ad1Curve kAd1Curve1(0.81459387, 1.58674746, 0.581142865, 1.89364943);
inline const Ad1Curve kAd1Curve2(1.26459138, 2.39715448, 0.856279228, 1.48562438);

// Effect section, 10 types (Synth1 manual: ctl1 / ctl2 meaning per type).
// Distortions (measured, measure_effect.py, reference/meta/effect.json):
// u = drive[ctl1] * x -> curve -> RBJ highpass -> RBJ lowpass at 400 Hz *
// 40^(ctl2 / 127), Q 3 -> output gain (level + 2) / 129 (NO dry mix).
//   1 a.d.2: smooth odd curve (kAd2Curve, limit 0.825), HP 100 Hz Q 2.29
//            (exact to 0.01 dB / 0.1 % residual).
//   2 d.d.:  triangle fold, F(x) = P tri(S x / P): slope S and peak P per
//            ctl1 (measure_dd.py, kDdSlope / kDdPeak; the peak falls from
//            1.64 at ctl1 0 to 0.5778 from ~64 up), same filters (nulls
//            -50..-75 dB, every ctl1).
//   0 a.d.1: out(ctl1) S(d(ctl1) x) (measured exact: measure_ad1_law.py),
//            d = 70 dB over ctl1, out = measured (kAd1Drive / kAd1Out); S is
//            a FITTED model (fit_ad1.py, median sample error -22..-24 dB,
//            TODO(measure) the exact structure): z = f1(d x - G lp(z) + b1)
//            - f1(b1) inside negative feedback through a 95.4 Hz one-pole on
//            the previous z (G 7.76), a bilinear one-pole HP 263.5 Hz, s =
//            f2(g2 h + b2) - f2(b2), one-pole HP 30.3 Hz, RBJ HP 96.3 Hz Q
//            2.37, the same RBJ LP (Q 3). Both curves are asymmetric soft
//            limits f(w) = w / (1 + |w / L|^k)^(1/k), read from tables.
//   3 deci.: sample & hold every ctl1 + 1 samples, floor quantiser q =
//            0.00643 * 2^(floor(ctl2 / 8) - 8) (integer bits), then (ctl1 > 0
//            only) a bilinear one-pole lowpass at 14 kHz; dry / wet crossfade
//            level / 128 (127 = fully wet).
//   4 r.m.:  x * sin, 8000^(ctl1 / 127) Hz (1 Hz .. 8 kHz); crossfade
//            level / 127.
//   5 comp.: one-pole detector of |x| PER CHANNEL (time constant per ctl2,
//            0.1 ms .. 0.2 s, attack == release), soft-knee leveller gain =
//            (Gmax^-4 + (env / 0.2604)^4)^(-1/4), Gmax = 10 + 0.3125 ctl1
//            dB; output gain -30 dB .. 0 dB with level (no dry).
//   6..9 phasers: 2 / 4 / 8 / 12 identical first-order allpasses (coefficient
//            from tan ~ pi p / fs, NOT prewarped), out = (x + w) / 2, w =
//            AP(x + k w[n-1]), feedback k = 0.99 (level - 64) / 64. Stage
//            frequency p = 254.6 Hz * 2^pos: pos starts at 0 and moves at
//            4 H f octaves / s, bouncing between 2.32 +- H, H = 0.02625 ctl1
//            octaves, f = 0.6418 Hz * 2^((ctl2 - 64) / 12.9). L = R.
// (measured: measure_effect.py; the sweep only advances while signal flows.)
class EffectUnit {
public:
    void Prepare(double sr) { sr_ = sr; }
    void Reset() {
        hp2_.Reset();
        lp2_.Reset();
        fb_[0] = fb_[1] = 0;
        for (auto &c : ad1Hp_)
            for (double &v : c) v = 0;
        for (auto &c : ap_)
            for (double &v : c) v = 0;
        hold_[0] = hold_[1] = held_[0] = held_[1] = 0;
        holdCount_ = 0;
        env_[0] = env_[1] = phase_ = 0;
        pos_ = 0, dir_ = 1;
        apFb_[0] = apFb_[1] = 0;
    }
    void Process(float *l, float *r, int n, const PatchValues &p) {
        const int type = p[kEffectType];
        if (type == 0) return Ad1(l, r, n, p);
        if (type <= 2) return Distort(l, r, n, p);
        if (type == 3) return Decimate(l, r, n, p);
        if (type == 5) return Compress(l, r, n, p);
        if (type >= 6) return Phase(l, r, n, p);
        const double m = p[kEffectLevel] / 127.0, inc = std::pow(8000.0, p[kEffectCtrl1] / 127.0) / sr_;
        for (int i = 0; i < n; ++i) {
            const double s = std::sin(2 * M_PI * phase_);
            phase_ += inc;
            phase_ -= std::floor(phase_);
            l[i] = float(l[i] * (1 - m + m * s));
            r[i] = float(r[i] * (1 - m + m * s));
        }
    }

private:
    // (fit_ad1.py) per channel
    void Ad1(float *l, float *r, int n, const PatchValues &p) {
        static constexpr double kBias1 = 0.724662338, kLoopGain = 7.76408919, kGain2 = 1.46001006, kBias2 = 0.233492458;
        const Ad1Curve &f1 = kAd1Curve1, &f2 = kAd1Curve2;
        const int c1 = std::clamp(p[kEffectCtrl1], 0, 127);
        const double drive = tables::kAd1Drive[c1], out = tables::kAd1Out[c1] * (p[kEffectLevel] + 2) / 129.0;
        hp2_.Set(96.2836231, 2.37189425, true, sr_);
        lp2_.Set(400.0 * std::pow(40.0, p[kEffectCtrl2] / 127.0), 3.0, false, sr_);
        const double a = 1.0 - std::exp(-2 * M_PI * 95.3658895 / sr_), z0 = f1(kBias1), s0 = f2(kBias2);
        const double k1 = std::tan(M_PI * 263.480241 / sr_), k2 = std::tan(M_PI * 30.2652942 / sr_);
        float *s[2] = {l, r};
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const double z = f1(drive * s[ch][i] - kLoopGain * fb_[ch] + kBias1) - z0;
                fb_[ch] += a * (z - fb_[ch]);
                double *h = ad1Hp_[ch]; // two bilinear one-pole highpasses: x1, y1 each
                const double h1 = (z - h[0]) / (1 + k1) + (1 - k1) / (1 + k1) * h[1];
                const double v = f2(kGain2 * h1 + kBias2) - s0;
                const double h2 = (v - h[2]) / (1 + k2) + (1 - k2) / (1 + k2) * h[3];
                h[0] = z, h[1] = h1, h[2] = v, h[3] = h2;
                s[ch][i] = float(out * lp2_.Process(ch, hp2_.Process(ch, h2)));
            }
    }
    static double Drive(const float *table, int c1) {
        const double x = std::clamp(c1, 0, 127) / 4.0;
        const int i = std::min(int(x), 31);
        const double t = std::min(1.0, (std::clamp(c1, 0, 127) - 4.0 * i) / (i == 31 ? 3.0 : 4.0));
        return std::exp(std::log(table[i]) * (1 - t) + std::log(table[i + 1]) * t);
    }
    static double Ad2(double u) {
        const double a = std::fabs(u) / 0.05;
        if (a >= 200) return std::copysign(tables::kAd2Curve[200], u);
        const int i = int(a);
        return std::copysign(tables::kAd2Curve[i] + (tables::kAd2Curve[i + 1] - tables::kAd2Curve[i]) * (a - i), u);
    }
    // triangle fold: slope 1 through 0, peaks +-1 at +-1, period 4
    static double Fold(double v) {
        const double a = std::fabs(v) + 1.0;
        const double ph = a < 4.0 ? a : std::fmod(a, 4.0);
        return (v < 0 ? -1.0 : 1.0) * (ph < 2.0 ? ph - 1.0 : 3.0 - ph);
    }

    void Distort(float *l, float *r, int n, const PatchValues &p) {
        const int type = p[kEffectType];
        const int c1 = std::clamp(p[kEffectCtrl1], 0, 127);
        const double drive = type == 1 ? Drive(tables::kAd2Drive, c1)
                                       : tables::kDdSlope[c1] / tables::kDdPeak[c1]; // d.d.: fold rate
        const double ddPeak = tables::kDdPeak[c1];
        hp2_.Set(100.0, 2.29, true, sr_);
        lp2_.Set(400.0 * std::pow(40.0, p[kEffectCtrl2] / 127.0), 3.0, false, sr_);
        const double out = (p[kEffectLevel] + 2) / 129.0;
        for (int i = 0; i < n; ++i) {
            float *s[2] = {l + i, r + i};
            for (int ch = 0; ch < 2; ++ch) {
                const double u = drive * *s[ch];
                const double z = type == 1 ? Ad2(u) : ddPeak * Fold(u);
                *s[ch] = float(out * lp2_.Process(ch, hp2_.Process(ch, z)));
            }
        }
    }

    void Decimate(float *l, float *r, int n, const PatchValues &p) {
        const int period = p[kEffectCtrl1] + 1;
        const double q = 0.00643 * std::exp2(p[kEffectCtrl2] / 8 - 8);
        const double m = p[kEffectLevel] >= 127 ? 1.0 : p[kEffectLevel] / 128.0;
        const double k = std::tan(M_PI * 14016.0 / sr_), b = k / (1 + k), a = (1 - k) / (1 + k);
        float *s[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            if (holdCount_ == 0) {
                hold_[0] = std::floor(l[i] / q) * q;
                hold_[1] = std::floor(r[i] / q) * q;
            }
            holdCount_ = (holdCount_ + 1) % period;
            for (int ch = 0; ch < 2; ++ch) {
                double w = hold_[ch];
                if (period > 1) { // bilinear one-pole: y = b (x + x1) + a y1
                    const double y = b * (w + held_[ch]) + a * apFb_[ch];
                    held_[ch] = w, apFb_[ch] = y, w = y;
                }
                s[ch][i] = float((1 - m) * s[ch][i] + m * w);
            }
        }
    }

    static double CompTau(int v) {
        static constexpr double c[11] = {0, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127};
        static constexpr double t[11] = {0.0001, 0.000643, 0.001455, 0.002735, 0.004578, 0.010796,
                                         0.022192, 0.041823, 0.074208, 0.125771, 0.199574};
        int i = 0;
        while (i < 9 && v > c[i + 1]) ++i;
        const double f = std::clamp((v - c[i]) / (c[i + 1] - c[i]), 0.0, 1.0);
        return std::exp(std::log(t[i]) * (1 - f) + std::log(t[i + 1]) * f);
    }
    void Compress(float *l, float *r, int n, const PatchValues &p) {
        const double a = 1.0 - std::exp(-1.0 / (CompTau(p[kEffectCtrl2]) * sr_));
        const double gmax4 = std::pow(10.0, -(10.0 + 0.3125 * p[kEffectCtrl1]) / 5.0); // Gmax^-4
        const double out = std::pow(10.0, 1.5 * (p[kEffectLevel] / 127.0 - 1.0));
        float *s[2] = {l, r};
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                env_[ch] += a * (std::fabs(s[ch][i]) - env_[ch]);
                const double e4 = std::pow(env_[ch] / 0.2604, 4.0);
                s[ch][i] = float(s[ch][i] * (out / std::sqrt(std::sqrt(gmax4 + e4))));
            }
    }

    void Phase(float *l, float *r, int n, const PatchValues &p) {
        static constexpr int kStages[4] = {2, 4, 8, 12};
        const int stages = kStages[std::clamp(p[kEffectType] - 6, 0, 3)];
        const double h = 0.02625 * p[kEffectCtrl1];
        const double step = 4.0 * h * 0.6418 * std::exp2((p[kEffectCtrl2] - 64) / 12.9) / sr_;
        const double k = 0.99 * (p[kEffectLevel] - 64) / 64.0;
        bool live = false;
        for (int i = 0; i < n && !live; ++i) live = l[i] != 0.0f || r[i] != 0.0f;
        for (int i = 0; i < n; ++i) {
            if (live && h > 0) {
                pos_ += dir_ * step;
                if (pos_ > 2.32 + h) dir_ = -1, pos_ = 2.0 * (2.32 + h) - pos_;
                else if (pos_ < 2.32 - h && dir_ < 0) dir_ = 1, pos_ = 2.0 * (2.32 - h) - pos_;
            }
            const double t = M_PI * 254.6 * std::exp2(pos_) / sr_, a = (t - 1) / (t + 1);
            float *s[2] = {l + i, r + i};
            for (int ch = 0; ch < 2; ++ch) {
                const double x = *s[ch];
                double y = x + k * apFb_[ch];
                for (int st = 0; st < stages; ++st) {
                    const double o = a * y + ap_[ch][st];
                    ap_[ch][st] = y - a * o;
                    y = o;
                }
                apFb_[ch] = y;
                *s[ch] = float(0.5 * (x + y));
            }
        }
    }

    double sr_ = 44100;
    Biquad hp2_, lp2_;
    double fb_[2] = {0, 0};
    double ad1Hp_[2][4] = {};
    double ap_[2][12] = {};
    double apFb_[2] = {0, 0};
    double hold_[2] = {0, 0}, held_[2] = {0, 0};
    int holdCount_ = 0;
    double env_[2] = {0, 0}, phase_ = 0, pos_ = 0, dir_ = 1;
};

} // namespace oxo::s2
