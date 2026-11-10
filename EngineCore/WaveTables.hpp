#pragma once

// Band-limited single-cycle tables, one per MIDI note (measured: the
// original's saw carries EVERY harmonic below Nyquist at exactly 1/n, alias
// free, and an oscillator whose note index exceeds 127 is silent). Built by
// inverse FFT once per sample rate and shared by every instance.
//
// Shapes (amplitude 1, fundamentals all in phase = sin(2*pi*phase)):
//   saw      falling, +1 at phase 0 (Synth1 >= 1.11)
//   triangle peak +1 at phase 0.25
// Pulse = (saw(p) - saw(p + d)) / 2, sine is computed.

#include "Simd.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace oxo::s2 {

inline void InverseRealFft(std::vector<std::complex<double>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// An oscillator phase as a 64-bit fixed-point fraction of a cycle: it wraps
// for free (the detune copies' phases).
using Phase64 = uint64_t;
// cycles taken mod 1 (negative steps included)
inline Phase64 ToPhase64(double cycles) {
    const double f = cycles >= 0.0 && cycles < 1.0 ? cycles : cycles - std::floor(cycles);
    return Phase64(int64_t(std::min(f, 0x1.fffffffffffffp-1) * 0x1p63)) << 1;
}

struct WaveTable {
    // 4-point Hermite interpolation of the samples, its cubic stored per point
    // (one load per read)
    struct Cubic {
        float c0, c1, c2, c3;
    };
    uint32_t size = 0; // power of two
    uint32_t bits = 0; // log2(size)
    std::vector<Cubic> cubic; // size + 1 (a phase of exactly 1 reads point 0 again)

    // phase in [0, 1]
    OXO_INLINE float Read(double phase) const {
        const double pos = phase * size;
        const uint32_t i = uint32_t(pos);
        return Eval(i, float(pos - i));
    }
    void SetSamples(const std::vector<float> &x) {
        size = uint32_t(x.size());
        for (bits = 0; (1u << bits) < size; ++bits) {}
        cubic.resize(size + 1);
        for (uint32_t i = 0; i <= size; ++i) {
            const float xm1 = x[(i + size - 1) % size], x0 = x[i % size], x1 = x[(i + 1) % size], x2 = x[(i + 2) % size];
            cubic[i] = {x0, 0.5f * (x1 - xm1), xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, 0.5f * (x2 - xm1) + 1.5f * (x0 - x1)};
        }
    }

private:
    OXO_INLINE float Eval(uint32_t i, float f) const {
        const Cubic &c = cubic[i];
        return ((c.c3 * f + c.c2) * f + c.c1) * f + c.c0;
    }
};

class WaveTableSet {
public:
    static constexpr int kNotes = 128;

    static std::shared_ptr<const WaveTableSet> ForSampleRate(double sr) {
        static std::mutex mutex;
        static std::vector<std::shared_ptr<const WaveTableSet>> cache;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &s : cache)
            if (s->sampleRate_ == sr) return s;
        auto set = std::shared_ptr<const WaveTableSet>(new WaveTableSet(sr));
        cache.push_back(set);
        return set;
    }

    const WaveTable &Saw(int note) const { return saw_[note]; }
    const WaveTable &Triangle(int note) const { return tri_[note]; }

    // The detune copies' tables. (measured, measure_copies.py) the original
    // reads osc1's and the sub's 8 copies from 2048-point tables of the
    // note's band-limited wave with the phase TRUNCATED, no interpolation:
    // every harmonic h leaves lines at (k * 2048 +- h) x the copy's
    // frequency, 1 / (k * 2048) of the fundamental (-66 dB, a sawtooth
    // series in k), for the sine, saw, triangle, pulse and the sub's shapes
    // alike. Ours the same: that is what its detune sounds like, and such a
    // read costs a third of an interpolated one. A copy's value = the table
    // point at its phase's top kCopyBits (Phase64 >> (64 - kCopyBits)); a
    // pulse = two points of the saw, the sub's square = the saw's
    // difference half a cycle apart, stored.
    static constexpr int kCopyBits = 11, kCopyPoints = 1 << kCopyBits;
    enum CopyShape { kCopySaw, kCopyTriangle, kCopySquare, kCopyShapes };
    const float *CopyTable(CopyShape shape, int note) const { return &copies_[(size_t(shape) * kNotes + size_t(note)) * kCopyPoints]; }
    const float *CopySine() const { return copySine_.data(); }

private:
    explicit WaveTableSet(double sr) : sampleRate_(sr), copies_(size_t(kCopyShapes) * kNotes * kCopyPoints), copySine_(kCopyPoints) {
        const auto sawAmp = [](int h) { return 2.0 / (M_PI * h); };
        const auto triAmp = [](int h) {
            if (!(h & 1)) return 0.0;
            return ((h >> 1) & 1 ? -8.0 : 8.0) / (M_PI * M_PI * h * h);
        };
        for (int n = 0; n < kNotes; ++n) {
            const double f = 440.0 * std::pow(2.0, (n - 69) / 12.0);
            const int harmonics = std::max(1, int(std::ceil(sr / 2 / f)) - 1);
            uint32_t size = 1024;
            while (size < uint32_t(4 * harmonics)) size <<= 1;
            saw_[n].SetSamples(Samples(size, harmonics, sawAmp));
            tri_[n].SetSamples(Samples(size, harmonics, triAmp));
            const std::vector<float> saw = Samples(kCopyPoints, harmonics, sawAmp), tri = Samples(kCopyPoints, harmonics, triAmp);
            float *copy[kCopyShapes];
            for (int shape = 0; shape < kCopyShapes; ++shape) copy[shape] = &copies_[(size_t(shape) * kNotes + size_t(n)) * kCopyPoints];
            for (int i = 0; i < kCopyPoints; ++i) {
                copy[kCopySaw][i] = saw[size_t(i)];
                copy[kCopyTriangle][i] = tri[size_t(i)];
                copy[kCopySquare][i] = 0.5f * (saw[size_t(i)] - saw[size_t((i + kCopyPoints / 2) % kCopyPoints)]);
            }
        }
        for (int i = 0; i < kCopyPoints; ++i) copySine_[size_t(i)] = float(std::sin(2 * M_PI * i / kCopyPoints));
    }

    // One cycle of the sine series x(p) = sum amp(h) * sin(2*pi*h*p), the
    // harmonics the size can hold.
    template <typename Amp> static std::vector<float> Samples(uint32_t size, int harmonics, Amp amp) {
        std::vector<std::complex<double>> spec(size);
        for (int h = 1; h <= harmonics && h < int(size / 2); ++h) {
            const double a = amp(h);
            spec[h] = std::complex<double>(0, -a / 2);
            spec[size - h] = std::complex<double>(0, a / 2);
        }
        InverseRealFft(spec);
        std::vector<float> x(size);
        for (uint32_t i = 0; i < size; ++i) x[i] = float(spec[i].real());
        return x;
    }

    double sampleRate_;
    WaveTable saw_[kNotes];
    WaveTable tri_[kNotes];
    std::vector<float> copies_; // [shape][note][kCopyPoints]
    std::vector<float> copySine_;
};

} // namespace oxo::s2
