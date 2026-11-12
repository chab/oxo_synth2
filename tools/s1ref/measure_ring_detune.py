#!/usr/bin/env python3
"""Ring on + osc1 detune + sub: the original's osc1 section (osc2 plays no
part; without detune ring leaves osc1 alone).

  measure_ring_detune.py

Osc1 sine + sub sine, detune 127 (copies resolvable), mix 0. The ring-off
render gives each copy's osc1 / sub component; the ring-on render is fitted
as A tanh((a O + b S) / A), O / S = the ring-off osc1 / sub parts. Measured:
a ~ 1 (osc1 unchanged), b ~ 1 / 0.297 (the sub copies skip the copy gain),
A ~ 2.2 (sub 16) .. 1.65 (sub 127), residual -22 .. -15 dB: the true bound
is not a fixed tanh. It is the post-filter limiter (AGENTS.md "Ring BUG"):
the compression falls with the filtered level, so the engine keeps only b.
"""
import numpy as np
from scipy.optimize import least_squares

import s1

SR = 44100
KEY = 60
F1 = s1.key_hz(KEY)
CENTS = np.array([-7, -5, -3, -1, 0, 1, 3, 5, 7]) * 20.0  # detune 127: d = 20 cents


def part(m, f0, t):
    fr = f0 * 2 ** (CENTS / 1200)
    M = np.stack([g(2 * np.pi * f * t) for f in fr for g in (np.cos, np.sin)], 1)
    c, *_ = np.linalg.lstsq(M, m, rcond=None)
    return M @ c


def main():
    for sub in (4, 16, 64, 127):
        specs = [dict(native=s1.patch(p0=0, p1=3, p2=71, p76=127, p95=sub, p96=0, p97=1, p7=r, p5=0, p91=1),
                      notes=((0, 2.5, KEY, 127),), length=2.5) for r in (0, 1)]
        (off, _), (on, _) = s1.render_many(specs)
        sl = slice(int(.3 * SR), int(2.3 * SR))
        t = np.arange(sl.stop - sl.start) / SR
        m = np.asarray(off[0], float)[sl] / 0.2
        y = np.asarray(on[0], float)[sl] / 0.2
        O, S = part(m, F1, t), part(m, F1 / 2, t)
        f = lambda p: p[0] * np.tanh((p[1] * O + p[2] * S) / p[0])
        r = least_squares(lambda p: f(p) - y, [1.8, 1, 3], bounds=([.2, -5, -20], [50, 5, 20]))
        res = 10 * np.log10(np.mean(r.fun ** 2) / np.mean(y ** 2))
        print(f"sub {sub:3d}: A {r.x[0]:.2f}  a {r.x[1]:.2f}  b {r.x[2]:.2f} (1/0.297 = 3.37)  residual {res:.1f} dB")


if __name__ == "__main__":
    main()
