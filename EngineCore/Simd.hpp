#pragma once

// Four floats in one register: SSE2 (every x86-64), NEON
// (every arm64), plain C++ elsewhere. The engine's hot copies (osc1's 8
// detune copies) run on it explicitly: their table reads are gathers, which
// no compiler vectorizes on its own (and MSVC vectorizes little else).
// MulAdd = a * b + c: fused on NEON (as clang contracts the scalar code on
// arm64), two roundings on SSE2 (as the scalar code without FMA).

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

namespace oxo::s2::simd {

#if OXO_SIMD_SSE2

struct F4 {
    __m128 v;
};
inline F4 Load(const float *p) { return {_mm_loadu_ps(p)}; }
inline F4 Set(float x) { return {_mm_set1_ps(x)}; }
// (from registers: four narrow stores then a wide load would stall store forwarding)
inline F4 FromInts(int32_t a, int32_t b, int32_t c, int32_t d) { return {_mm_cvtepi32_ps(_mm_setr_epi32(a, b, c, d))}; }
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

#elif OXO_SIMD_NEON

struct F4 {
    float32x4_t v;
};
inline F4 Load(const float *p) { return {vld1q_f32(p)}; }
inline F4 Set(float x) { return {vdupq_n_f32(x)}; }
inline F4 FromInts(int32_t a, int32_t b, int32_t c, int32_t d) {
    return {vcvtq_f32_s32(vsetq_lane_s32(d, vsetq_lane_s32(c, vsetq_lane_s32(b, vdupq_n_s32(a), 1), 2), 3))};
}
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

#else

struct F4 {
    float v[4];
};
template <typename Op> inline F4 Map(F4 a, F4 b, Op op) {
    return {{op(a.v[0], b.v[0]), op(a.v[1], b.v[1]), op(a.v[2], b.v[2]), op(a.v[3], b.v[3])}};
}
inline F4 Load(const float *p) { return {{p[0], p[1], p[2], p[3]}}; }
inline F4 Set(float x) { return {{x, x, x, x}}; }
inline F4 FromInts(int32_t a, int32_t b, int32_t c, int32_t d) { return {{float(a), float(b), float(c), float(d)}}; }
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

#endif

} // namespace oxo::s2::simd
