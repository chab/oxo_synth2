#!/usr/bin/env python3
"""a.d.1 (effect type 0): fit the model of Effects.hpp (ad1_model.c) to the
original, time domain, sample by sample.

  fit_ad1.py               drive / output gain per ctl1 knot (structure kept)
  fit_ad1.py --structure   also refit the structure (at ctl1 64)
  fit_ad1.py --ours        score our engine instead (no fit)

-> "ad1" in reference/meta/effect.json (gen_tables.py: kAd1Drive / kAd1Out).

Test set per ctl1 (ctl2 127, level 127): sines at 49..1568 Hz x 5 levels, a
saw at key 36 and noise x 3 levels; input = the effect-off render with the
EQ tone stage (22 kHz bilinear one-pole after the effect) undone, output =
the model through that stage again, compared on 0.25..0.32 s (fit) /
0.25..0.45 s (score), each test normalized to its rms.

Model (2026-11-11, median sample error -17.5..-24 dB over ctl1, worst
tests -10..-14 dB: 49 Hz sines and loud saws): z = f(d x - G lp(z) + b) -
f(b), lp = one-pole lowpass 82.7 Hz on the previous z (G 8.34), f = w / (1 +
|w / L|^k)^(1/k) per side (L+ 0.606 k+ 1.31, L- 0.934 k- 2.93, b 0.333),
then highpasses 227.6 Hz and 22 Hz (bilinear one-poles), RBJ highpass 97.3
Hz Q 2.56, RBJ lowpass 400 Hz 40^(ctl2 / 127) Q 3 (exactly the other
distortions' lowpass), gain out(ctl1) (level + 2) / 129. Only d and out
depend on ctl1 (refitting the bias per ctl1 gains nothing); d * out
follows the measured small-signal gain within 1 %. A free piecewise-linear
curve does not fit better: the remaining error is structural (TODO(measure)).
"""
import ctypes
import json
import subprocess
import sys

import numpy as np
from scipy import optimize
from scipy.signal import lfilter

import s1

SR = 44100.0
KNOTS = [0, 4, 8, 12, 16, 24, 32, 48, 64, 80, 96, 112, 127]
NAMES = ["drive", "loop_gain", "loop_hz", "pos_limit", "neg_limit", "pos_knee", "neg_knee", "bias",
         "hp1_hz", "hp2_hz", "hp_hz", "hp_q", "out", "lp_hz", "lp_q"]

LIB = s1.ROOT / "build/ad1_model.dylib"
subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(s1.ROOT / "tools/s1ref/ad1_model.c"), "-o", str(LIB)], check=True)
lib = ctypes.CDLL(str(LIB))
dp = ctypes.POINTER(ctypes.c_double)
lib.run.argtypes = [dp, ctypes.c_int, dp, ctypes.c_double, dp]

_k = np.pi * 22000.0 / SR
TG, TB = _k / (1 + _k), (1 - _k) / (1 + _k)


def tests():
    t = [(dict(p0=0, p5=0, p29=g), k) for k in (31, 43, 55, 67, 79, 91) for g in (16, 32, 64, 96, 127)]
    for g in (16, 64, 127):
        t += [(dict(p0=1, p5=0, p29=g), 36), (dict(p0=2, p8=0, p5=127, p1=4, p29=g), 60)]
    return t


def render(c1, ours=False):
    specs = []
    for kw, key in tests():
        notes = ((0, 0.6, key, 127),)
        specs.append(dict(native=s1.patch(**kw, p77=1, p78=0, p79=c1, p80=127, p81=127), notes=notes, length=0.6))
        specs.append(dict(native=s1.patch(**kw), notes=notes, length=0.6))
    o = [np.asarray(a[0], float) for a, _ in s1.render_many(specs, ours=ours)]
    return [(lfilter([1 / TG, -TB / TG], [1, 1], x), y) for x, y in zip(o[1::2], o[0::2])]


def sim(v, p):
    v = np.ascontiguousarray(v, np.float64)
    p = np.ascontiguousarray(p, np.float64)
    y = np.empty_like(v)
    lib.run(v.ctypes.data_as(dp), len(v), p.ctypes.data_as(dp), SR, y.ctypes.data_as(dp))
    return lfilter([TG, TG], [1, -TB], y)


def errors(p, data, seg):
    a, b = int(seg[0] * SR), int(seg[1] * SR)
    out = []
    for v, y in data:
        out.append((sim(v[:b], p)[a:] - y[a:b]) / (np.sqrt(np.mean(y[a:b] ** 2)) + 1e-9))
    return out


def score(p, data):
    return np.array([20 * np.log10(np.sqrt(np.mean(e ** 2))) for e in errors(p, data, (0.25, 0.45))])


def fit(p, data, free):
    def r(q):
        pp = p.copy()
        pp[free] = np.exp(q)
        return np.concatenate(errors(pp, data, (0.25, 0.32)))
    res = optimize.least_squares(r, np.log(p[free]), max_nfev=300)
    p = p.copy()
    p[free] = np.exp(res.x)
    return p


def main():
    path = s1.ROOT / "reference/meta/effect.json"
    meta = json.loads(path.read_text())
    ad1 = meta["ad1"]
    p = np.array([ad1[n] if n in ad1 else 0.0 for n in NAMES])
    p[13] = 16000.0
    if "--ours" in sys.argv:
        for c1 in KNOTS:
            p[0], p[12] = ad1["drive_per_c1"][KNOTS.index(c1)], ad1["out_per_c1"][KNOTS.index(c1)]
            e = score(p, render(c1, ours=True))  # model vs ours (implementation check)
            print(f"ctl1 {c1:3d}: ours vs the model, median {np.median(e):.1f} dB")
        return
    if "--structure" in sys.argv:
        p[0] = ad1["drive_per_c1"][KNOTS.index(64)]
        p[12] = ad1["out_per_c1"][KNOTS.index(64)]
        p = fit(p, render(64), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])
    drive, out = [], []
    for c1 in KNOTS:
        data = render(c1)
        best = None
        for d0, o0 in ((ad1["drive_per_c1"][KNOTS.index(c1)], ad1["out_per_c1"][KNOTS.index(c1)]),):
            q = p.copy()
            q[0], q[12] = d0, o0
            q = fit(q, data, [0, 12])
            e = score(q, data)
            if best is None or np.median(e) < best[0]:
                best = (np.median(e), q, e.max())
        print(f"ctl1 {c1:3d}: drive {best[1][0]:.6g} out {best[1][12]:.6g}  median {best[0]:.1f} dB, worst {best[2]:.1f} dB",
              flush=True)
        drive.append(float(best[1][0]))
        out.append(float(best[1][12]))
    for i, n in enumerate(NAMES):
        if n not in ("drive", "out", "lp_hz"):
            ad1[n] = float(p[i])
    ad1["c1"], ad1["drive_per_c1"], ad1["out_per_c1"] = KNOTS, drive, out
    path.write_text(json.dumps(meta, indent=1))
    print("wrote", path)


if __name__ == "__main__":
    main()
