#pragma once

// Four floats in one register: SSE2 (every x86-64), NEON
// (every arm64), plain C++ elsewhere. The engine's hot copies (osc1's 8
// detune copies) run on it explicitly: their table reads are gathers, which
// no compiler vectorizes on its own (and MSVC vectorizes little else).
// MulAdd = a * b + c: fused on NEON (as clang contracts the scalar code on
// arm64), two roundings on SSE2 (as the scalar code without FMA).
// P4 = four 64-bit oscillator phases (fractions of a cycle) in two
// registers: Phases (from four values), Load / StorePhases, +, ShiftLeft,
// Top24 (each phase's top 24 bits as a float in [0, 1)). clang builds the
// same code from scalar source on its own, MSVC does not.

#include <cmath>
#include <cstdint>

#if defined(OXO_SIMD_SCALAR) // (tests: the plain C++ fallback)
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define OXO_SIMD_SSE2 1
#elif defined(__ARM_NEON) || defined(_M_ARM64)
#include <arm_neon.h>
#define OXO_SIMD_NEON 1
#endif

// The engine's per-sample helpers: always inlined (a call in the voice's
// loop spills its many live values; MSVC inlines less than clang).
#if defined(_MSC_VER) && !defined(__clang__)
#define OXO_INLINE __forceinline
#else
#define OXO_INLINE inline __attribute__((always_inline))
#endif

namespace oxo::s2::simd {

#if OXO_SIMD_SSE2

struct F4 {
    __m128 v;
};
inline F4 Load(const float *p) { return {_mm_loadu_ps(p)}; }
inline F4 Set(float x) { return {_mm_set1_ps(x)}; }
inline F4 operator+(F4 a, F4 b) { return {_mm_add_ps(a.v, b.v)}; }
inline F4 operator-(F4 a, F4 b) { return {_mm_sub_ps(a.v, b.v)}; }
inline F4 operator*(F4 a, F4 b) { return {_mm_mul_ps(a.v, b.v)}; }
inline F4 MulAdd(F4 a, F4 b, F4 c) { return {_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)}; }
inline F4 Min(F4 a, F4 b) { return {_mm_min_ps(a.v, b.v)}; }
inline F4 Abs(F4 a) { return {_mm_andnot_ps(_mm_set1_ps(-0.0f), a.v)}; }
// |mag| with the sign of -s (mag >= 0)
inline F4 NegSignOf(F4 mag, F4 s) { return {_mm_xor_ps(mag.v, _mm_andnot_ps(s.v, _mm_set1_ps(-0.0f)))}; }
inline float Sum(F4 a) {
    const __m128 h = _mm_add_ps(a.v, _mm_movehl_ps(a.v, a.v));
    return _mm_cvtss_f32(_mm_add_ss(h, _mm_shuffle_ps(h, h, 1)));
}
// rows a..d -> columns (a = every row's x, b = y, ...)
inline void Transpose(F4 &a, F4 &b, F4 &c, F4 &d) { _MM_TRANSPOSE4_PS(a.v, b.v, c.v, d.v); }

struct P4 {
    __m128i a, b;
};
inline P4 LoadPhases(const uint64_t *p) {
    return {_mm_loadu_si128(reinterpret_cast<const __m128i *>(p)), _mm_loadu_si128(reinterpret_cast<const __m128i *>(p + 2))};
}
inline void StorePhases(uint64_t *p, P4 x) {
    _mm_storeu_si128(reinterpret_cast<__m128i *>(p), x.a);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(p + 2), x.b);
}
// (from registers: narrow stores then a wide load would stall store forwarding)
inline P4 Phases(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return {_mm_set_epi64x(int64_t(b), int64_t(a)), _mm_set_epi64x(int64_t(d), int64_t(c))};
}
inline P4 operator+(P4 x, P4 y) { return {_mm_add_epi64(x.a, y.a), _mm_add_epi64(x.b, y.b)}; }
inline P4 ShiftLeft(P4 x, int n) {
    const __m128i count = _mm_cvtsi32_si128(n);
    return {_mm_sll_epi64(x.a, count), _mm_sll_epi64(x.b, count)};
}
inline F4 Top24(P4 x) {
    const __m128 lo = _mm_castsi128_ps(_mm_srli_epi64(x.a, 40)), hi = _mm_castsi128_ps(_mm_srli_epi64(x.b, 40));
    const __m128i bits = _mm_castps_si128(_mm_shuffle_ps(lo, hi, _MM_SHUFFLE(2, 0, 2, 0)));
    return {_mm_mul_ps(_mm_cvtepi32_ps(bits), _mm_set1_ps(0x1p-24f))};
}

#elif OXO_SIMD_NEON

struct F4 {
    float32x4_t v;
};
inline F4 Load(const float *p) { return {vld1q_f32(p)}; }
inline F4 Set(float x) { return {vdupq_n_f32(x)}; }
inline F4 operator+(F4 a, F4 b) { return {vaddq_f32(a.v, b.v)}; }
inline F4 operator-(F4 a, F4 b) { return {vsubq_f32(a.v, b.v)}; }
inline F4 operator*(F4 a, F4 b) { return {vmulq_f32(a.v, b.v)}; }
inline F4 MulAdd(F4 a, F4 b, F4 c) { return {vfmaq_f32(c.v, a.v, b.v)}; }
inline F4 Min(F4 a, F4 b) { return {vminq_f32(a.v, b.v)}; }
inline F4 Abs(F4 a) { return {vabsq_f32(a.v)}; }
inline F4 NegSignOf(F4 mag, F4 s) {
    const uint32x4_t sign = vandq_u32(vmvnq_u32(vreinterpretq_u32_f32(s.v)), vdupq_n_u32(0x80000000u));
    return {vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(mag.v), sign))};
}
inline float Sum(F4 a) {
    const float32x2_t h = vadd_f32(vget_low_f32(a.v), vget_high_f32(a.v));
    return vget_lane_f32(vpadd_f32(h, h), 0);
}
inline void Transpose(F4 &a, F4 &b, F4 &c, F4 &d) {
    const float32x4x2_t ab = vtrnq_f32(a.v, b.v), cd = vtrnq_f32(c.v, d.v);
    a.v = vcombine_f32(vget_low_f32(ab.val[0]), vget_low_f32(cd.val[0]));
    b.v = vcombine_f32(vget_low_f32(ab.val[1]), vget_low_f32(cd.val[1]));
    c.v = vcombine_f32(vget_high_f32(ab.val[0]), vget_high_f32(cd.val[0]));
    d.v = vcombine_f32(vget_high_f32(ab.val[1]), vget_high_f32(cd.val[1]));
}

struct P4 {
    uint64x2_t a, b;
};
inline P4 LoadPhases(const uint64_t *p) { return {vld1q_u64(p), vld1q_u64(p + 2)}; }
inline void StorePhases(uint64_t *p, P4 x) {
    vst1q_u64(p, x.a);
    vst1q_u64(p + 2, x.b);
}
inline P4 Phases(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return {vcombine_u64(vcreate_u64(a), vcreate_u64(b)), vcombine_u64(vcreate_u64(c), vcreate_u64(d))};
}
inline P4 operator+(P4 x, P4 y) { return {vaddq_u64(x.a, y.a), vaddq_u64(x.b, y.b)}; }
inline P4 ShiftLeft(P4 x, int n) {
    const int64x2_t count = vdupq_n_s64(n);
    return {vshlq_u64(x.a, count), vshlq_u64(x.b, count)};
}
inline F4 Top24(P4 x) {
    return {vcvtq_n_f32_u32(vcombine_u32(vmovn_u64(vshrq_n_u64(x.a, 40)), vmovn_u64(vshrq_n_u64(x.b, 40))), 24)};
}

#else

struct F4 {
    float v[4];
};
template <typename Op> inline F4 Map(F4 a, F4 b, Op op) {
    return {{op(a.v[0], b.v[0]), op(a.v[1], b.v[1]), op(a.v[2], b.v[2]), op(a.v[3], b.v[3])}};
}
inline F4 Load(const float *p) { return {{p[0], p[1], p[2], p[3]}}; }
inline F4 Set(float x) { return {{x, x, x, x}}; }
inline F4 operator+(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x + y; }); }
inline F4 operator-(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x - y; }); }
inline F4 operator*(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x * y; }); }
inline F4 MulAdd(F4 a, F4 b, F4 c) { return a * b + c; }
inline F4 Min(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return y < x ? y : x; }); }
inline F4 Abs(F4 a) { return Map(a, a, [](float x, float) { return std::fabs(x); }); }
inline F4 NegSignOf(F4 mag, F4 s) { return Map(mag, s, [](float m, float x) { return std::signbit(x) ? m : -m; }); }
inline float Sum(F4 a) { return (a.v[0] + a.v[2]) + (a.v[1] + a.v[3]); }
inline void Transpose(F4 &a, F4 &b, F4 &c, F4 &d) {
    const F4 r[4] = {a, b, c, d};
    F4 *out[4] = {&a, &b, &c, &d};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i]->v[j] = r[j].v[i];
}

struct P4 {
    uint64_t v[4];
};
inline P4 LoadPhases(const uint64_t *p) { return {{p[0], p[1], p[2], p[3]}}; }
inline void StorePhases(uint64_t *p, P4 x) {
    for (int k = 0; k < 4; ++k) p[k] = x.v[k];
}
inline P4 Phases(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return {{a, b, c, d}}; }
inline P4 operator+(P4 x, P4 y) { return {{x.v[0] + y.v[0], x.v[1] + y.v[1], x.v[2] + y.v[2], x.v[3] + y.v[3]}}; }
inline P4 ShiftLeft(P4 x, int n) { return {{x.v[0] << n, x.v[1] << n, x.v[2] << n, x.v[3] << n}}; }
inline F4 Top24(P4 x) {
    return {{float(x.v[0] >> 40) * 0x1p-24f, float(x.v[1] >> 40) * 0x1p-24f, float(x.v[2] >> 40) * 0x1p-24f,
             float(x.v[3] >> 40) * 0x1p-24f}};
}

#endif

// While alive, denormals are flushed to zero (FTZ + DAZ): on x86 each
// operation on one costs ~100 cycles (feedback tails and filter states
// fading out in silence); arm64 runs them at full speed (no-op there).
// The caller's (host thread's) mode comes back at the end.
class ScopedNoDenormals {
public:
#if OXO_SIMD_SSE2
    ScopedNoDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040); }
    ~ScopedNoDenormals() { _mm_setcsr(saved_); }
#else
    ScopedNoDenormals() {}
    ~ScopedNoDenormals() {}
#endif
    ScopedNoDenormals(const ScopedNoDenormals &) = delete;
    ScopedNoDenormals &operator=(const ScopedNoDenormals &) = delete;

#if OXO_SIMD_SSE2
private:
    unsigned saved_;
#endif
};

} // namespace oxo::s2::simd
