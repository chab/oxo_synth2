#pragma once

// Four 64-bit oscillator phases (fractions of a cycle, WaveTables.hpp
// Phase64) in two registers: SSE2 (every x86-64), NEON (every arm64), plain
// C++ elsewhere (OXO_SIMD_SCALAR forces it). The detune copies' phases step
// through it: Phases (from four values), Load / StorePhases, +. clang builds
// the same code from scalar source on its own, MSVC does not.

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

#elif OXO_SIMD_NEON

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

#else

struct P4 {
    uint64_t v[4];
};
inline P4 LoadPhases(const uint64_t *p) { return {{p[0], p[1], p[2], p[3]}}; }
inline void StorePhases(uint64_t *p, P4 x) {
    for (int k = 0; k < 4; ++k) p[k] = x.v[k];
}
inline P4 Phases(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return {{a, b, c, d}}; }
inline P4 operator+(P4 x, P4 y) { return {{x.v[0] + y.v[0], x.v[1] + y.v[1], x.v[2] + y.v[2], x.v[3] + y.v[3]}}; }

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
