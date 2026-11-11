#!/usr/bin/env python3
"""Knobs at 0 that the fitted tables leave a hair above 0: does the original
modulate at all there? Prints the original next to ours (s2render).

LFO depth 0 on pitch: full bend at range 12 sits exactly on a 1/128-octave
step (1200 cents), so the smallest LFO pull below it would drop one step
during a square LFO's negative half. The original stays at 1200 (depth 1:
1200 +- 9.375, i.e. bend and LFO truncated separately; ours truncates the
sum, +9.375 / -18.75: open).

FM 0: osc1 sine, osc2 a fifth up, mix on osc1: any FM shows as sidebands
at f1 +- f2. The original has none (its -178 dB floor), FM 1 = -58 dB."""
import numpy as np
from scipy.signal import windows

import s1

SR = 44100


def pitch_cents(x, win=0.25):
    n, out = int(win * SR), []
    for i in range(0, len(x) - n, n):
        sp = np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n), 16 * n))
        k = np.argmax(sp)
        a, b, c = np.log(sp[k - 1:k + 2] + 1e-30)
        out.append(1200 * np.log2((k + 0.5 * (a - c) / (a - 2 * b + c)) * SR / (16 * n) / s1.key_hz(60)))
    return np.array(out)[2:-2]


def level(x, hz):
    n = len(x)
    w = windows.kaiser(n, 36)
    sp = np.abs(np.fft.rfft(x * w)) / np.sum(w) * 2
    k = int(round(hz * n / SR))
    return 20 * np.log10(np.max(sp[k - 3:k + 4]) + 1e-300)


def main():
    print("LFO on pitch at depth 0 / 1, full bend (range 12): cents above C4 per 250 ms")
    for depth in (0, 1):
        patch = {**s1.DRY, 0: 0, 57: 1, 41: 2, 42: 2, 43: 30, 44: depth, 40: 12}
        spec = dict(native=patch, notes=((0.1, 5.9, 60, 100),), length=6.0, bends=((0.0, 16383),))
        for ours in (False, True):
            x, _ = s1.render_many([spec], ours=ours)[0]
            print(f"  depth {depth} {'ours    ' if ours else 'original'}", np.round(pitch_cents(x[0]), 2))
    f1, f2 = s1.key_hz(60), s1.key_hz(67)
    print("FM 0 / 1: sidebands re the carrier, dB (f1 - f2, f1 + f2)")
    for shape2, name in ((3, "tri"), (1, "saw")):
        for fm in (0, 1):
            patch = {**s1.DRY, 0: 0, 1: shape2, 2: 64 + 7, 5: 0, 45: fm}
            spec = dict(native=patch, notes=((0.0, 3.0, 60, 100),), length=3.0)
            for ours in (False, True):
                x, _ = s1.render_many([spec], ours=ours)[0]
                x = x[0][int(0.5 * SR):int(2.5 * SR)].astype(np.float64)
                c = level(x, f1)
                print(f"  osc2 {name} FM {fm} {'ours    ' if ours else 'original'}"
                      f" {level(x, f2 - f1) - c:7.1f} {level(x, f1 + f2) - c:7.1f}")


if __name__ == "__main__":
    main()
