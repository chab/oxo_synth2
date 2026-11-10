#pragma once

// Synth1 filter section, identified from the original's complex transfer
// functions (tools/s1ref/measure_filter.py + fit_*.py):
//
//  LP12 / HP12 / BP12 (types 0, 2, 3): Chamberlin state-variable filter run
//    TWICE per sample with the input held. f = 2 sin(pi fc / (2 fs)),
//    fc = 16.7 Hz * 2^(c / 12.624) (c clamped at 125), damping d = 1 - r,
//    input gain g = 1 - 0.7 r (r = res / 127). Fits within ~0.1-0.3 dB.
//    Followed by a level-riding limiter (AGC: peak follower 1.2 ms attack /
//    0.2 s release, gain (T / e)^0.883 above T = 1.427), which is what keeps
//    high resonance bounded with < 1 % THD.
//  LP24 (type 1): 4 bilinear one-pole stages, global feedback through a one-
//    sample delay, k = (0.0297 res - 0.076) * comp[cutoff], no input gain
//    compensation (passband = 1 / (1 + k), Moog-like).
//  LPDL (type 4): fitted linear diode ladder (see SetDiode) with a measured
//    input saturator (its drive falls with resonance) and a saturating
//    feedback (bounds the resonance: the peak drops 15 dB from small to full
//    level, self-oscillation stays nearly sinusoidal).
//
// Cutoff is driven in KNOB units (fractional, modulation adds to the knob);
// signals are normalized (oscillator full scale = 1).

#include "MeasuredTables.hpp"
#include "Simd.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace oxo::s2 {

// Coefficients vs cutoff knob (1/32 knob steps, linear interpolation), one
// set per sample rate shared by every voice: keeps per-sample cutoff
// modulation free of transcendental calls.
class FilterTables {
public:
    static constexpr int kSteps = 32;
    static constexpr int kSize = 127 * kSteps + 2;

    static std::shared_ptr<const FilterTables> ForSampleRate(double sr) {
        static std::mutex mutex;
        static std::vector<std::shared_ptr<const FilterTables>> cache;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &t : cache)
            if (t->sampleRate_ == sr) return t;
        auto t = std::shared_ptr<const FilterTables>(new FilterTables(sr));
        cache.push_back(t);
        return t;
    }

    // SVF f (cutoff clamped at 125), ladder one-pole a and feedback comp.
    double SvfF(double knob) const { return At(svfF_, knob); }
    void Ladder(double knob, double &ladderA, double &ladderComp) const {
        ladderA = At(ladderA_, knob);
        ladderComp = At(comp_, knob);
    }

private:
    static double At(const std::vector<double> &table, double knob) {
        const double x = std::clamp(knob, 0.0, 127.0) * kSteps;
        const int i = std::min(int(x), kSize - 2);
        const double t = x - i;
        return table[i] + (table[i + 1] - table[i]) * t;
    }

    explicit FilterTables(double sr) : sampleRate_(sr), svfF_(kSize), ladderA_(kSize), comp_(kSize) {
        for (int k = 0; k < kSize; ++k) {
            const double knob = std::min(double(k) / kSteps, 127.0);
            const double hz = 16.7 * std::exp2(std::min(knob, 125.0) / 12.624);
            svfF_[k] = 2.0 * std::sin(M_PI * std::min(hz, sr) / (2.0 * sr));
            const int i = std::min(int(knob), 126);
            const double t = knob - i;
            const double lhz = std::exp(std::log(tables::kLadderCutoffHz[i]) * (1 - t) +
                                        std::log(tables::kLadderCutoffHz[i + 1]) * t);
            const double g = std::tan(M_PI * std::min(lhz, sr * 0.49) / sr);
            ladderA_[k] = g / (1 + g);
            comp_[k] = tables::kLadderFeedbackComp[i] * (1 - t) + tables::kLadderFeedbackComp[i + 1] * t;
        }
    }

    double sampleRate_;
    std::vector<double> svfF_, ladderA_, comp_;
};

class VoiceFilter {
public:
    void Prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        tables_ = FilterTables::ForSampleRate(sampleRate);
        agcAttack_ = 1.0 - std::exp(-1.0 / (0.0012 * sampleRate));
        agcRelease_ = 1.0 - std::exp(-1.0 / (0.2017 * sampleRate));
    }

    void Reset() {
        low_ = band_ = 0;
        for (double &s : stage_) s = 0;
        fb_ = 0;
        agcEnv_ = 0;
        ladderGain_ = 1;
        for (double &s : diode_) s = 0;
        diodeX_ = 0;
    }

    // type 0..4, cutoff in knob units (may be fractional / out of range), res 0..127.
    OXO_INLINE void SetParams(int type, double cutoffKnob, int res) {
        type_ = type;
        if (type == 4) return SetDiode(cutoffKnob, res);
        if (type == 1) {
            double comp;
            tables_->Ladder(cutoffKnob, ladderA_, comp);
            ladderK_ = std::max(0.0, 0.0297 * res - 0.076) * comp;
            return;
        }
        svfF_ = tables_->SvfF(cutoffKnob);
        if (res != svfRes_) {
            const double r = res / 127.0;
            svfRes_ = res;
            svfD_ = std::max(1.0 - r, 0.0);
            svfG_ = 1.0 - 0.7 * r;
        }
    }

    OXO_INLINE double Process(double x) {
        if (type_ == 4) return Diode(x);
        if (type_ == 1) {
            // The limiter rides the FEEDBACK here (in-loop), which is what
            // bounds the measured self-oscillation at res 127 (~1.8 FS).
            double u = x - ladderK_ * ladderGain_ * fb_;
            for (double &s : stage_) {
                const double v = (u - s) * ladderA_;
                const double y = v + s;
                s = y + v;
                u = y;
            }
            fb_ = u;
            const double a = std::fabs(u);
            agcEnv_ += (a > agcEnv_ ? agcAttack_ : agcRelease_) * (a - agcEnv_);
            ladderGain_ = agcEnv_ > kLadderAgcThreshold ? std::pow(kLadderAgcThreshold / agcEnv_, kAgcExponent) : 1.0;
            return u;
        }
        const double in = svfG_ * x;
        double high = 0;
        for (int i = 0; i < 2; ++i) {
            low_ += svfF_ * band_;
            high = in - low_ - svfD_ * band_;
            band_ += svfF_ * high;
        }
        const double y = type_ == 0 ? low_ : type_ == 2 ? high : band_;
        const double a = std::fabs(y);
        agcEnv_ += (a > agcEnv_ ? agcAttack_ : agcRelease_) * (a - agcEnv_);
        return agcEnv_ > kAgcThreshold ? y * std::pow(kAgcThreshold / agcEnv_, kAgcExponent) : y;
    }

private:
    // LPDL (fitted, tools/s1ref/fit_lpdl.py): linear 303-style diode ladder,
    // dy1 = w(-2 y1 + y2 + u), dy2 = w(y1 - 2 y2 + y3), dy3 = w(y2 - 2 y3 +
    // y4), dy4 = w(y3 - y4), u = x - k (y4 - z), dz = 2 pi 100 (y4 - z) (the
    // feedback is high-passed: resonance keeps the bass), out = g (y3 + y4) / 2;
    // w / k / g tabulated per res x cutoff, trapezoidal integration.
    // The feedback saturation (gain G <= 1 on k) is applied as the explicit
    // extra input w k (1 - G) f from the state before the step, so the
    // linear (G = 1) solve stays implicit and precomputed.
    void SetDiode(double cutoffKnob, int res) {
        // coefficients follow the cutoff in 1/8-knob steps (a 5x5 inversion each)
        if (res == diodeRes_ && std::fabs(cutoffKnob - diodeCut_) < 0.125) return;
        diodeRes_ = res, diodeCut_ = cutoffKnob;
        static constexpr int kRes[9] = {0, 16, 32, 48, 64, 80, 96, 112, 127};
        int ri = 0;
        while (ri < 7 && res > kRes[ri + 1]) ++ri;
        const double rt = std::clamp(double(res - kRes[ri]) / (kRes[ri + 1] - kRes[ri]), 0.0, 1.0);
        const double c = std::clamp(cutoffKnob, 0.0, 127.0) / 4.0;
        const int ci = std::min(int(c), 30);
        const double ct = c - ci; // > 1 above knob 124: extrapolates the last step
        auto at = [&](const float *t, bool logScale) {
            auto row = [&](int r) {
                const double a = t[r * 32 + ci], b = t[r * 32 + ci + 1];
                return logScale ? std::exp(std::log(a) + (std::log(b) - std::log(a)) * ct) : a + (b - a) * std::min(ct, 1.0);
            };
            return row(ri) + (row(ri + 1) - row(ri)) * rt;
        };
        const double w = at(tables::kLpdlW, true), k = at(tables::kLpdlK, false);
        diodeG_ = at(tables::kLpdlG, false);
        if (w == diodeW_ && k == diodeK_) return;
        diodeW_ = w, diodeK_ = k;
        const double h = 0.25 / sampleRate_, wh = 2 * M_PI * 100.0; // 2x oversampled (as the original)
        const double A[5][5] = {{-2 * w, w, 0, -k * w, k * w}, {w, -2 * w, w, 0, 0}, {0, w, -2 * w, w, 0},
                                {0, 0, w, -w, 0}, {0, 0, 0, wh, -wh}};
        // M = I - h A (inverted), N = I + h A; x' = M^-1 (N x + h B (u0 + u1)), B = w e1
        double M[5][10] = {};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j) {
                M[i][j] = (i == j) - h * A[i][j];
                diodeN_[i][j] = (i == j) + h * A[i][j];
            }
        for (int i = 0; i < 5; ++i) M[i][5 + i] = 1;
        for (int i = 0; i < 5; ++i) { // Gauss-Jordan, partial pivoting
            int piv = i;
            for (int r = i + 1; r < 5; ++r)
                if (std::fabs(M[r][i]) > std::fabs(M[piv][i])) piv = r;
            if (piv != i)
                for (int j = 0; j < 10; ++j) std::swap(M[i][j], M[piv][j]);
            const double p = 1.0 / M[i][i];
            for (double &v : M[i]) v *= p;
            for (int r = 0; r < 5; ++r)
                if (r != i && M[r][i] != 0) {
                    const double f = M[r][i];
                    for (int j = 0; j < 10; ++j) M[r][j] -= f * M[i][j];
                }
        }
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j) diodeMi_[i][j] = M[i][5 + j];
        diodeB_ = h * w;
    }
    // tanh(x) / x, Lambert's continued fraction (|error| < 1e-5 below 4.97)
    static double TanhOverX(double x) {
        const double x2 = x * x;
        if (x2 > 24.7) return 1.0 / std::fabs(x);
        return (135135 + x2 * (17325 + x2 * (378 + x2))) / (135135 + x2 * (62370 + x2 * (3150 + 28 * x2)));
    }
    double Diode(double x) {
        // input attenuated with resonance (the output gain g grows with it),
        // then saturated; the output undoes the attenuation
        const double sc = kLpdlInputRef / diodeG_, u = kLpdlDrive * sc * x;
        const double xs = u * TanhOverX(u) / kLpdlDrive;
        const double mid = 0.5 * (diodeX_ + xs);
        // feedback saturation tanh(c k f) / c (f = y4 - z) = gain G on k,
        // from the state at the start of the sample
        const double kf = diodeK_ * (diode_[3] - diode_[4]);
        const double deficit = kf * (1 - TanhOverX(kLpdlFeedbackSat * kf));
        DiodeStep(diodeX_ + deficit, mid + deficit);
        DiodeStep(mid + deficit, xs + deficit);
        diodeX_ = xs;
        return diodeG_ / sc * 0.5 * (diode_[2] + diode_[3]);
    }
    void DiodeStep(double u0, double u1) {
        double v[5];
        for (int i = 0; i < 5; ++i) {
            v[i] = 0;
            for (int j = 0; j < 5; ++j) v[i] += diodeN_[i][j] * diode_[j];
        }
        v[0] += diodeB_ * (u0 + u1);
        for (int i = 0; i < 5; ++i) {
            double s = 0;
            for (int j = 0; j < 5; ++j) s += diodeMi_[i][j] * v[j];
            diode_[i] = std::clamp(s, -50.0, 50.0);
        }
    }

    // LPDL nonlinearity (measured, measure_lpdl_nl.py, 2026-10-29; peak /
    // h3 / compression vs level): input x * kLpdlInputRef / g through
    // tanh(kLpdlDrive u) / kLpdlDrive, feedback through tanh(c k f) / c
    static constexpr double kLpdlInputRef = 1.3, kLpdlDrive = 1.0, kLpdlFeedbackSat = 2.0;
    static constexpr double kAgcThreshold = 1.427;
    static constexpr double kAgcExponent = 0.883;
    static constexpr double kLadderAgcThreshold = 1.6; // ear/plot-tuned vs res 112..127 step responses

    double sampleRate_ = 44100;
    std::shared_ptr<const FilterTables> tables_;
    int type_ = 1;
    double svfF_ = 0.1, svfD_ = 1, svfG_ = 1;
    int svfRes_ = 0; // the res svfD_ / svfG_ hold
    double low_ = 0, band_ = 0;
    double ladderA_ = 0.5, ladderK_ = 0, ladderGain_ = 1;
    double stage_[4] = {0, 0, 0, 0};
    double fb_ = 0;
    double agcAttack_ = 0.1, agcRelease_ = 0.001, agcEnv_ = 0;
    double diode_[5] = {}, diodeX_ = 0, diodeG_ = 1, diodeW_ = -1, diodeK_ = -1, diodeB_ = 0, diodeCut_ = -1e9;
    int diodeRes_ = -1;
    double diodeN_[5][5] = {}, diodeMi_[5][5] = {};
};

// (measured) filter saturation, after the filter: y = (1 + a) x / (1 + a |x|).
OXO_INLINE double Saturate(double x, double a) { return a > 0 ? (1 + a) * x / (1 + a * std::fabs(x)) : x; }

} // namespace oxo::s2
