// The filters' output limiter (Filter.hpp VoiceFilter::Limit), for
// measure_filter_limiter.py: a follower of |x| (attack / release one-pole
// coefficients) and the soft-knee gain (1 + (e / T)^k)^(-p / k).
#include <math.h>

void limiter(const double *x, double *y, int n, double T, double p, double k, double att, double rel) {
    double e = 0;
    for (int i = 0; i < n; ++i) {
        const double a = fabs(x[i]);
        e += (a > e ? att : rel) * (a - e);
        y[i] = x[i] * pow(1.0 + pow(e / T, k), -p / k);
    }
}
