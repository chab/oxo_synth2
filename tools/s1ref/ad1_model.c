/* The a.d.1 model of EngineCore/Effects.hpp (EffectUnit::Ad1), one channel,
   double precision, for fit_ad1.py (built on the fly into build/).
   p: 0 drive, 1 loop gain G, 2 loop lowpass Hz, 3 / 4 positive / negative
   limit, 5 / 6 their knees, 7 bias, 8 / 9 post highpasses Hz, 10 / 11 RBJ
   highpass Hz / Q, 12 output gain, 13 / 14 RBJ lowpass Hz / Q. */
#include <math.h>

static double side(double w, double l, double k) { return w / pow(1 + pow(fabs(w / l), k), 1 / k); }
static double curve(double w, const double *p) { return w >= 0 ? side(w, p[3], p[5]) : side(w, p[4], p[6]); }

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;
static void rbj(Biquad *q, double fc, double Q, int hp, double sr) {
    if (fc > sr * 0.49) fc = sr * 0.49;
    double w = 2 * M_PI * fc / sr, al = sin(w) / (2 * Q), c = cos(w), a0 = 1 + al;
    double k = hp ? (1 + c) / 2 : (1 - c) / 2;
    q->b0 = k / a0; q->b1 = (hp ? -2 * k : 2 * k) / a0; q->b2 = k / a0; q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0;
    q->z1 = q->z2 = 0;
}
static double biquad(Biquad *q, double x) {
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

void run(const double *x, int n, const double *p, double sr, double *y) {
    Biquad hp, lp;
    rbj(&hp, p[10], p[11], 1, sr);
    rbj(&lp, p[13], p[14], 0, sr);
    const double a = 1 - exp(-2 * M_PI * p[2] / sr), f0 = curve(p[7], p);
    double k1 = tan(M_PI * p[8] / sr), k2 = tan(M_PI * p[9] / sr), fb = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    for (int i = 0; i < n; ++i) {
        const double z = curve(p[0] * x[i] - p[1] * fb + p[7], p) - f0;
        fb += a * (z - fb);
        const double h1 = (z - x1) / (1 + k1) + (1 - k1) / (1 + k1) * y1;
        x1 = z, y1 = h1;
        const double h2 = (h1 - x2) / (1 + k2) + (1 - k2) / (1 + k2) * y2;
        x2 = h1, y2 = h2;
        y[i] = p[12] * biquad(&lp, biquad(&hp, h2));
    }
}
