#!/usr/bin/env python3
"""a.d.1 (effect type 0): fit the model of Effects.hpp (ad1_model.c) to the
original, time domain, sample by sample.

  fit_ad1.py           score the model in effect.json against the original
  fit_ad1.py --fit     refit its structure (all ctl1 at once) -> effect.json "ad1"
  fit_ad1.py --ours    score our engine against the model (implementation check)

ctl1 is not fitted: measure_ad1_law.py measured that the original is
out(ctl1) S(d(ctl1) x) for one system S, with d = d64 10^(3.5 (ctl1 - 64) /
127) and out = out64 ad1_law.out[ctl1]; only S's parameters are fitted
here (d64 and out64 included), on ctl1 0, 8, .., 127 together.

Test set per ctl1 (ctl2 127, level 127): sines at 49..1568 Hz x 5 levels, a
saw at key 36 and noise x 3 levels; input = the effect-off render with the
EQ tone stage (22 kHz bilinear one-pole after the effect) undone, output =
the model through that stage again, compared on 0.25..0.32 s (fit) /
0.25..0.45 s (score), each test normalized to its rms.

Model (2026-11-12, median sample error -22.5 dB over ctl1, worst tests
-13..-16 dB; the single-curve model before: -18.7 / -10..-15): a curve f1
inside negative feedback through a one-pole lowpass (the original switches
like a hard clip at high ctl1, with the hysteresis of such a loop), a
one-pole highpass, a second curve f2 (the original's response to an edge
depends on the previous edge: up to x1.58 after 60 samples, which only a
curve AFTER a highpass does), then one-pole HP, RBJ HP and the
distortions' RBJ lowpass (Q 3). f(w) = w / (1 + |w / L|^k)^(1/k) per side.
Not the exact structure (TODO(measure)): the edge ratio decays faster
than the original's (1.05 at 97 samples vs 1.38).
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
C1S = [0, 8, 16, 24, 32, 48, 64, 96, 127]
NAMES = ["drive64", "loop_gain", "loop_hz", "f1_pos_limit", "f1_neg_limit", "f1_pos_knee", "f1_neg_knee", "f1_bias",
         "hp1_hz", "hp2_hz", "hp_hz", "hp_q", "out64", "lp_hz", "lp_q", "f2_gain", "f2_bias", "f2_pos_limit",
         "f2_pos_knee", "f2_neg_limit", "f2_neg_knee"]
FIXED = {"lp_hz", "lp_q"}
LOG = [n not in ("f1_bias", "f2_bias") for n in NAMES]

LIB = s1.ROOT / "build/ad1_model.dylib"
subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(s1.ROOT / "tools/s1ref/ad1_model.c"), "-o", str(LIB)], check=True)
lib = ctypes.CDLL(str(LIB))
dp = ctypes.POINTER(ctypes.c_double)
lib.run.argtypes = [dp, ctypes.c_int, dp, ctypes.c_double, dp]

_k = np.pi * 22000.0 / SR
TG, TB = _k / (1 + _k), (1 - _k) / (1 + _k)
META = s1.ROOT / "reference/meta/effect.json"


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


def at(p, c1, law):
    """the parameter vector for one ctl1 (ctl2 127, level 127)."""
    q = np.array(p, float)
    q[0] = p[0] * 10 ** (3.5 * (c1 - 64) / 127)
    q[12] = p[12] * law[c1]
    return q


def sim(v, p):
    v = np.ascontiguousarray(v, np.float64)
    p = np.ascontiguousarray(p, np.float64)
    y = np.empty_like(v)
    lib.run(v.ctypes.data_as(dp), len(v), p.ctypes.data_as(dp), SR, y.ctypes.data_as(dp))
    return lfilter([TG, TG], [1, -TB], y)


def errors(p, data, law, seg):
    a, b = int(seg[0] * SR), int(seg[1] * SR)
    out = []
    for c1, cases in data.items():
        q = at(p, c1, law)
        for v, y in cases:
            out.append((sim(v[:b], q)[a:] - y[a:b]) / (np.sqrt(np.mean(y[a:b] ** 2)) + 1e-9))
    return out


def scores(p, data, law):
    e = [20 * np.log10(np.sqrt(np.mean(x ** 2))) for x in errors(p, data, law, (0.25, 0.45))]
    return np.array(e).reshape(len(data), -1)


def report(p, data, law, what):
    s = scores(p, data, law)
    for c1, r in zip(data, s):
        print(f"ctl1 {c1:3d}: {what} median {np.median(r):6.1f} dB, worst {r.max():6.1f} dB")
    print(f"all: median {np.median(s):.2f} dB", flush=True)


def main():
    meta = json.loads(META.read_text())
    law = meta["ad1_law"]["out"]
    p = np.array([meta["ad1"][n] for n in NAMES])
    if "--ours" in sys.argv:
        report(p, {c1: render(c1, ours=True) for c1 in C1S}, law, "ours vs the model:")
        return
    data = {c1: render(c1) for c1 in C1S}
    if "--fit" not in sys.argv:
        report(p, data, law, "model vs the original:")
        return
    free = [i for i, n in enumerate(NAMES) if n not in FIXED]
    lg = np.array(LOG)[free]

    def unpack(q):
        pp = p.copy()
        pp[free] = np.where(lg, np.exp(q), q)
        return pp
    q0 = np.where(lg, np.log(np.abs(p[free])), p[free])
    for _ in range(2):
        r = optimize.least_squares(lambda q: np.concatenate(errors(unpack(q), data, law, (0.25, 0.32))), q0,
                                   x_scale="jac", max_nfev=150)
        q0 = r.x
        report(unpack(q0), data, law, "model vs the original:")
    p = unpack(q0)
    meta["ad1"] = {n: float(v) for n, v in zip(NAMES, p)}
    META.write_text(json.dumps(meta, indent=1))
    print("wrote", META)


if __name__ == "__main__":
    main()
