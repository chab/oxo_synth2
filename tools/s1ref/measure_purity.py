#!/usr/bin/env python3
"""What a held note carries besides its expected lines (the harmonics of
osc1 and of its 8 detune copies at +-1, 3, 5, 7 x d cents): aliasing,
interpolation images, intermodulation. Original next to ours (s2render).
LP24 at resonance 0 (our ladder is linear there; LP12's level limiter adds
its own intermodulation to a loud detune sum, in both).

Per render: the strongest unexpected line (dB re the strongest line, Hz),
the power outside the expected lines (dB re the total), the median floor."""
import sys

import numpy as np
from scipy.signal import windows

import s1

SR = 44100
N = 1 << 17
W = windows.kaiser(N, 38)
MASK_HZ = 8.0


def residue(x, expected):
    sp = np.abs(np.fft.rfft(x[:N] * W)) / np.sum(W) * 2
    f = np.arange(len(sp)) * SR / N
    keep = f >= 20
    for hz in expected:
        keep &= np.abs(f - hz) > MASK_HZ
    rest = sp[keep]
    k = np.argmax(rest)
    return (20 * np.log10(rest[k] / sp.max()), f[keep][k], 10 * np.log10(np.sum(rest ** 2) / np.sum(sp ** 2)),
            20 * np.log10(np.median(rest) / sp.max()))


def main(keys=(36, 48, 60, 72, 84, 96), detunes=(0, 26, 80)):
    specs, meta = [], []
    for key in keys:
        for detune in detunes:
            for shape in (0, 1, 2):
                patch = {**s1.DRY, 0: shape, 76: detune, 5: 0, 19: 127, 14: 1, 20: 0}
                specs.append(dict(native=patch, notes=((0.0, 3.9, key, 100),), length=4.0))
                meta.append((key, detune, shape))
    renders = [s1.render_many(specs, ours=ours) for ours in (False, True)]
    print("key det shape | original: line dB @ Hz, outside dB, floor dB | ours")
    for i, (key, detune, shape) in enumerate(meta):
        d = 20.0 * detune / 127.0
        cents = [0.0] + ([sign * k * d for k in (1, 3, 5, 7) for sign in (-1, 1)] if detune else [])
        expected = []
        for c in cents:
            f = s1.key_hz(key) * 2 ** (c / 1200)
            expected += [h * f for h in range(1, (int(SR / 2 / f) if shape else 1) + 1)]
        cols = ["%7.1f @%8.1f %7.1f %7.1f" % residue(r[i][0][0][int(0.4 * SR):].astype(np.float64), expected) for r in renders]
        print(f"{key:3d} {detune:3d} {('sin', 'saw', 'pul')[shape]}   | {cols[0]} | {cols[1]}")


if __name__ == "__main__":
    main(*([tuple(int(k) for k in sys.argv[1].split(","))] if len(sys.argv) > 1 else []))
