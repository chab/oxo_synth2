#!/usr/bin/env python3
"""How the original reads osc1's 8 detune copies (and the sub's): from a
2048-point table of the note's band-limited wave, the phase TRUNCATED, no
interpolation. A truncated read leaves, for every harmonic h of a copy at
frequency f, lines at (k * 2048 +- h) * f (folded), 1 / (k * 2048) of the
copy's fundamental whatever h (a sawtooth series in k: -66.2, -72.2 dB...),
and none beyond the wave's highest harmonic. An interpolated read leaves
none above ~-110 dB.

The probe plays osc1 quietly (mix 122, osc2 pitched above Nyquist: silent
from key 84 up) so the lines stand clear of what a loud detune sum adds
(measure_purity.py). Prints, original then ours (s2render), the median
level of the lines found per h, dB re the copies' fundamentals.

  measure_copies.py [keys]   e.g. 84,96 (default 96)"""
import sys

import numpy as np
from scipy.signal import find_peaks, windows

import s1

SR = 44100
N = 1 << 18
W = windows.kaiser(N, 34)
POINTS = 2048
DETUNE = 26
CASES = [("sine", {0: 0}, False), ("saw", {0: 1}, False), ("pulse 64", {0: 2}, False), ("pulse 30", {0: 2, 8: 30}, False),
         ("triangle", {0: 3}, False), ("sub sine", {0: 0, 95: 127, 96: 0, 97: 1}, True),
         ("sub square", {0: 0, 95: 127, 96: 3, 97: 1}, True)]


def fold(f):
    f %= SR
    return SR - f if f > SR / 2 else f


def lines(x):
    db = 20 * np.log10(np.abs(np.fft.rfft(x[:N] * W)) / np.sum(W) * 2 + 1e-300)
    out = []
    for k in find_peaks(db, height=-170, distance=6)[0]:
        a, b, c = db[k - 1:k + 2]
        o = 0.5 * (a - c) / (a - 2 * b + c)
        out.append(((k + o) * SR / N, b - 0.25 * (a - c) * o))
    return out


def series(x, f0):
    """median level of the lines at (k * POINTS +- h) * f per (k, h), dB re the copies' fundamentals"""
    found = lines(x)
    count = int(SR / 2 / f0)
    d = 20.0 * DETUNE / 127.0
    copies = [f0 * 2 ** (sign * k * d / 1200) for k in (1, 3, 5, 7) for sign in (-1, 1)]
    level = np.mean([max((a for f, a in found if abs(f - c) < 1.0), default=-999) for c in copies])
    by = {}
    for f, a in found:
        if any(abs(f - h * c) < 2 for c in copies + [f0] for h in range(1, count + 1)):
            continue
        hits = [(abs(fold((k * POINTS + sign * h) * c) - f), k, h) for k in (1, 2) for h in range(1, 2 * count + 3)
                for sign in (-1, 1) for c in copies]
        err, k, h = min(hits)
        if err < 0.3:
            by.setdefault((k, h), []).append(a - level)
    return count, {key: np.median(v) for key, v in by.items()}


def main(keys=(96,)):
    for key in keys:
        for name, patch, sub in CASES:
            native = {**s1.DRY, 76: DETUNE, 5: 122, 2: 127, 1: 1, 19: 127, 14: 1, 20: 0, **patch}
            spec = dict(native=native, notes=((0.0, 7.9, key, 100),), length=8.0)
            print(f"key {key}, {name}:")
            for ours in (False, True):
                x = s1.render_many([spec], ours=ours)[0][0][0][int(0.5 * SR):].astype(np.float64)
                count, by = series(x, s1.key_hz(key) / (2 if sub else 1))
                shown = min(count, 12)
                for k in (1, 2):
                    row = " ".join(f"{by[(k, h)]:5.0f}" if (k, h) in by else "    -" for h in range(1, shown + 1))
                    beyond = [v for (kk, h), v in by.items() if kk == k and h > count]
                    print(f"  {'ours    ' if ours else 'original'} {k} x {POINTS} +- h, h = 1..{shown}: {row}"
                          + (f"   beyond h = {count}: {max(beyond):.0f} at most" if beyond else ""))


if __name__ == "__main__":
    main(*([tuple(int(k) for k in sys.argv[1].split(","))] if len(sys.argv) > 1 else []))
