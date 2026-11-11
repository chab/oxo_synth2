#!/usr/bin/env python3
"""d.d. (effect type 2) per ctl1: the fold's slope and peak, all 128 values.

  measure_dd.py   -> "dd_slope" / "dd_peak" in reference/meta/effect.json

Model (nulls -50..-73 dB): y = EQtone(LP(HP(F(x)))) * (level + 2) / 129,
F(x) = P tri(S x / P), tri = a triangle of period 4 and peak 1 (F folds
at +-P with slope +-S). The probe: a sine (key 45) from an 8-voice
coherent unison stack (x up to ~6.6, so even ctl1 0 folds a few times),
effect off = x through the EQ tone stage (the 22 kHz bilinear one-pole
after the effect), undone at the sine's frequency to get the effect's
input. HP 100 Hz Q 2.29, LP at ctl2 127, EQ tone as in Effects.hpp. Each
ctl1: a coarse scan of the fold rate S / P around the previous value, two
refinements, P by least squares. The peak is NOT constant: 1.64 at ctl1 0
falling to 0.5778 from ~64 up (the old model: one threshold, 0.5835).
"""
import json

import numpy as np
from scipy.signal import hilbert, lfilter

import s1

SR = 44100
KEY = 45
BASE = dict(p0=0, p5=0, p29=127, p73=1, p93=8, p75=0, p84=64, p91=1, p92=0)
SEG = slice(8000, 40000)


def rbj(fc, q, hp):
    w = 2 * np.pi * min(fc, SR * 0.49) / SR
    al, c = np.sin(w) / (2 * q), np.cos(w)
    k = (1 + c) / 2 if hp else (1 - c) / 2
    a0 = 1 + al
    return np.array([k, -2 * k if hp else 2 * k, k]) / a0, np.array([1, -2 * c / a0, (1 - al) / a0])


def tri(v):
    ph = np.fmod(np.abs(v) + 1, 4)
    return np.sign(v) * np.where(ph < 2, ph - 1, 3 - ph)


def main():
    k = np.pi * 22000 / SR
    beq, aeq = np.array([k, k]) / (1 + k), np.array([1, -(1 - k) / (1 + k)])
    specs = [dict(native=s1.patch(**BASE), notes=((0, 1.0, KEY, 127),), length=1.0)]
    specs += [dict(native=s1.patch(**BASE, p77=1, p78=2, p79=c, p80=127, p81=127), notes=((0, 1.0, KEY, 127),), length=1.0)
              for c in range(128)]
    outs = s1.render_many(specs)
    x = outs[0][0][0].astype(float)
    z = np.exp(-2j * np.pi * s1.key_hz(KEY) / SR)
    h = (beq[0] + beq[1] * z) / (1 - (1 - k) / (1 + k) * z)
    v = np.real(hilbert(x) * np.exp(-1j * np.angle(h))) / abs(h)
    (bh, ah), (bl, al) = rbj(100, 2.29, True), rbj(400 * 40, 3.0, False)
    model = lambda f: lfilter(beq, aeq, lfilter(bl, al, lfilter(bh, ah, f)))[SEG]
    db = lambda s: 20 * np.log10(np.sqrt(np.mean(s ** 2)) + 1e-15)

    def fit(y, rates):
        best = None
        for q in rates:
            m = model(tri(q * v))
            p = np.dot(m, y) / np.dot(m, m)
            e = db(y - p * m) - db(y)
            if best is None or e < best[0]:
                best = (e, q, p)
        return best

    slope, peak, rate, growth = [], [], 0.55, 1.0
    for c in range(128):
        y = outs[1 + c][0][0].astype(float)[SEG]
        # the error is rugged in the rate (thousands of folds up top): scan
        # around the predicted rate, steps under 1 / (folds)
        guess = rate * growth
        e, q, p = fit(y, np.geomspace(guess * 0.75, guess * 1.35, 400) if c < 40 else
                      np.geomspace(guess * 0.97, guess * 1.03, 3000))
        e, q, p = fit(y, np.linspace(q * 0.9995, q * 1.0005, 201))
        e, q, p = fit(y, np.linspace(q * 0.99999, q * 1.00001, 101))
        growth, rate = (q / rate if c else 1.0), q
        slope.append(float(q * p))
        peak.append(float(p))
        print(f"ctl1 {c:3d}  slope {q * p:11.5f}  peak {p:.5f}  null {e:6.1f} dB", flush=True)
    path = s1.ROOT / "reference/meta/effect.json"
    meta = json.loads(path.read_text())
    meta["dd_slope"], meta["dd_peak"] = slope, peak
    path.write_text(json.dumps(meta, indent=1))


if __name__ == "__main__":
    main()
