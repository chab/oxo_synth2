#include "TestFramework.hpp"
#include "../EngineCore/Lfo.hpp"

#include <cmath>
#include <vector>

using namespace oxo::s2;

namespace {
// One cycle (exactly `n` samples, from phase 0) of an LFO type's values and pitch steps.
void Cycle(int type, int pitchSteps, int n, std::vector<double> &v, std::vector<int32_t> &s) {
    Lfo lfo;
    lfo.Reset();
    v.assign(size_t(n), 0.0), s.assign(size_t(n), 0);
    lfo.Render(v.data(), n, type, 1.0, double(n), s.data(), pitchSteps);
}
} // namespace

// The original's step laws per waveform (measure_lfo_pitch.py)
OXO_TEST(LfoPitchStepsFollowTheMeasuredLaws) {
    OXO_EXPECT_EQ(Lfo::PitchSteps(1), 1);
    OXO_EXPECT_EQ(Lfo::PitchSteps(4), 5);
    OXO_EXPECT_EQ(Lfo::PitchSteps(127), 640);
    std::vector<double> v;
    std::vector<int32_t> s;
    const int n = 4000;
    // triangle: a step is taken when the wave reaches it (rising floor, falling ceil)
    Cycle(1, 2, n, v, s);
    for (int i = 0; i < n; ++i) {
        const double x = 2 * v[size_t(i)];
        if (std::fabs(x - std::round(x)) < 1e-9) continue; // on a step (the peak / trough)
        OXO_EXPECT_EQ(s[size_t(i)], int32_t(i < n / 2 ? std::floor(x) : std::ceil(x)));
    }
    OXO_EXPECT_EQ(s[n / 4 - 10], -1); // rising, just below 0
    OXO_EXPECT_EQ(s[3 * n / 4 - 10], 1); // falling, just above 0
    // saw: 2N + 1 equal bands, +N .. -N
    Cycle(0, 1, n, v, s);
    OXO_EXPECT_EQ(s[0], 1);
    OXO_EXPECT_EQ(s[n / 3 - 5], 1);
    OXO_EXPECT_EQ(s[n / 3 + 5], 0);
    OXO_EXPECT_EQ(s[2 * n / 3 + 5], -1);
    // sine: sign(v) floor(N |v| + 1/4): depth 1 switches at |v| = 0.75
    Cycle(5, 1, n, v, s);
    for (int i = 0; i < n; ++i) {
        const double x = v[size_t(i)];
        if (std::fabs(x) < 0.749) OXO_EXPECT_EQ(s[size_t(i)], 0);
        if (std::fabs(x) > 0.751) OXO_EXPECT_EQ(s[size_t(i)], x > 0 ? 1 : -1);
    }
    // S&H / smooth random: floor(N v), so -N .. N - 1
    for (const int type : {3, 4}) {
        Lfo lfo;
        lfo.Reset();
        std::vector<double> vv(100000);
        std::vector<int32_t> ss(vv.size());
        lfo.Render(vv.data(), int(vv.size()), type, 200.0, 44100.0, ss.data(), 3);
        for (size_t i = 0; i < vv.size(); ++i) {
            OXO_EXPECT_EQ(ss[i], int32_t(std::floor(3 * vv[i])));
            OXO_EXPECT(ss[i] >= -3 && ss[i] <= 2);
        }
    }
}

// (measure_lfo_random_keysync.py) a key-sync restart draws a new random value
// (S&H); smooth random ramps from the previous one to it (continuous), on amp
// from minus it to minus the previous one
OXO_TEST(LfoRandomRestartDrawsANewValue) {
    Lfo sh, smooth, amp;
    double a = 0, b = 0, r[2], q[2];
    sh.Render(&a, 1, 3, 0.01, 44100.0);
    sh.Restart(), smooth.Restart(), amp.Restart();
    sh.Render(&b, 1, 3, 0.01, 44100.0);
    OXO_EXPECT(b != a);
    for (int i = 0; i < 2; ++i) { // phases 0 and 1/2
        smooth.Render(r + i, 1, 4, 1.0, 2.0);
        amp.Render(q + i, 1, 4, 1.0, 2.0, nullptr, 0, true);
    }
    OXO_EXPECT_EQ(r[0], a);
    OXO_EXPECT(std::fabs(r[1] - (a + b) / 2) < 1e-12);
    OXO_EXPECT_EQ(q[0], -b);
    OXO_EXPECT(std::fabs(q[1] - -(a + b) / 2) < 1e-12);
}

// On amp (measured, pulse-probe gain): a square / S&H step ramps linearly to
// the next 256-sample boundary (a step before the first knot starts there);
// saw / triangle / sine reach each knot's value at the next knot (32 samples
// above 10 Hz, 64 up to it); a restart snaps.
OXO_TEST(LfoOnAmpMovesOnTheControlGrid) {
    std::vector<double> v(512);
    const auto run = [&](LfoAmpShaper &s, int type, double hz, double before, int step, double after) {
        for (int i = 0; i < 512; ++i) v[size_t(i)] = i < step ? before : after;
        s.Process(v.data(), 512, type, hz, 0, 512);
    };
    LfoAmpShaper sq;
    run(sq, 2, 20.0, 1.0, 156, -1.0); // a step at 156: 100 samples to the boundary
    OXO_EXPECT_EQ(v[155], 1.0);
    OXO_EXPECT(std::fabs(v[156] - (1.0 - 2.0 / 100)) < 1e-12);
    OXO_EXPECT(std::fabs(v[205] - 0.0) < 1e-12);
    OXO_EXPECT(std::fabs(v[255] - -1.0) < 1e-12);
    OXO_EXPECT_EQ(v[300], -1.0);
    LfoAmpShaper early, earlySlow;
    run(early, 2, 20.0, -1.0, 10, 1.0); // before the first knot (32 at 20 Hz): from 32
    OXO_EXPECT_EQ(v[31], -1.0);
    OXO_EXPECT(std::fabs(v[32] - (-1.0 + 2.0 / 224)) < 1e-12);
    run(earlySlow, 3, 2.0, -1.0, 10, 1.0); // 64 at 2 Hz
    OXO_EXPECT_EQ(v[63], -1.0);
    OXO_EXPECT(std::fabs(v[64] - (-1.0 + 2.0 / 192)) < 1e-12);
    LfoAmpShaper saw, slowSaw;
    run(saw, 0, 20.0, -1.0, 40, 1.0); // a jump at 40: knot 64 takes it, reached at 96
    OXO_EXPECT_EQ(v[63], -1.0);
    OXO_EXPECT_EQ(v[64], -1.0);
    OXO_EXPECT(std::fabs(v[80] - 0.0) < 1e-12);
    OXO_EXPECT_EQ(v[96], 1.0);
    run(slowSaw, 0, 2.0, -1.0, 40, 1.0); // 64-sample knots: 64 .. 128
    OXO_EXPECT(std::fabs(v[96] - 0.0) < 1e-12);
    OXO_EXPECT_EQ(v[128], 1.0);
    sq.Snap();
    run(sq, 2, 20.0, 0.5, 0, 0.5);
    OXO_EXPECT_EQ(v[0], 0.5);
}
