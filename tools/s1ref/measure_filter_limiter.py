#!/usr/bin/env python3
"""The LP12 / HP12 / BP12 / LP24 output limiter, and the LP24 self-oscillation
level through it.

  measure_filter_limiter.py [--selfosc]

The original bounds loud signals AFTER the filter, before the saturation and
the amp: filtered quietly (cutoff 35) a loud sum is not compressed, amp gain
and velocity do not change it, LP12 = LP24 at cutoff 127. It is a level-riding
gain, not a waveshaper (lines next to the carriers, almost no 3rd harmonic).

Loud inputs come from the ring + detune + sub quirk (the sub copies skip their
gain, Voice.hpp): detune 1 keeps the copies coherent with ours. Our engine
built with -DOXO_FILTER_LINEAR (no limiter, no AGC) gives the limiter's input,
the original its output; the fit (limiter_model.c) takes a follower of |y|
(attack, release) and the gain (1 + (e / T)^k)^(-p / k). Plain full-scale
sines at low keys weigh triple: the original leaves them untouched (a softer
knee or a mean-abs follower costs them 0.1..0.25 dB). Measured 2026-11-12:
T 1.038, p 0.856, k 11.5, attack 3.7 ms, release 6.9 ms, residual -28..-52 dB
per case (the ring cases' ~2 % level offset left free). TRIED, worse: a
mean-abs follower with a softer knee (-0.1..-0.25 dB on plain low sines),
gain steps at zero crossings, one follower per polarity.

--selfosc: noise into LP24 res 127 per cutoff, the oscillation line's
amplitude (original 1.72 at cutoff 56..96, 1.66 / 1.59 / 1.41 / 1.11 at 104 /
112 / 120 / 127): kLadderAgcThreshold (the in-loop AGC) sets ours through the
output limiter. OPEN: the original also suppresses the input noise ~21 dB
while oscillating (ours ~10), oscillates ~1..2 % higher, and its onset at
cutoffs <= 48 differs (near-critical loop gain, the comp table).
"""
import ctypes
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import welch

import s1
import wav

SR = 44100
LIB = s1.ROOT / "build/limiter_model.dylib"
LINEAR = s1.ROOT / "build/s2render_linear"


def build():
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(s1.ROOT / "tools/s1ref/limiter_model.c"), "-o", str(LIB)],
                   check=True)
    subprocess.run(["clang++", "-std=c++20", "-O2", "-DOXO_FILTER_LINEAR", "-o", str(LINEAR),
                    str(s1.ROOT / "tools/s2render/main.cpp")], check=True)


def render(jobs, binary=None):
    """jobs: (native, notes, length, extra job lines); binary None = the original."""
    with tempfile.TemporaryDirectory() as d:
        texts = [s1.job_text(Path(d) / f"{i}.wav", nat, notes, length=length).replace(
            "end\n", "".join(l + "\n" for l in extra) + "end\n") for i, (nat, notes, length, extra) in enumerate(jobs)]
        if binary:
            subprocess.run([str(binary), "-"], input="".join(texts).encode(), check=True, capture_output=True)
        else:
            s1.render_batch("".join(texts))
        return [np.asarray(wav.read(Path(d) / f"{i}.wav")[0][0], float) / 0.2 for i in range(len(jobs))]


def cases():
    ring = dict(p0=0, p5=0, p7=1, p14=1, p76=1, p95=127, p96=0, p97=0)
    J, names = [], []
    for key in (24, 36, 48, 60, 72, 84):
        for sub in (32, 127):
            J.append((s1.patch(**{**ring, "p95": sub}), ((0, 1.5, key, 127),), 1.5, []))
            names.append(f"1tone k{key} s{sub}")
    for key in (36, 48, 60):  # the sub an octave down
        J.append((s1.patch(**{**ring, "p97": 1}), ((0, 1.5, key, 127),), 1.5, []))
        names.append(f"2tone k{key}")
    for key in (36, 60):  # loud -> quiet -> loud (the sub gain)
        J.append((s1.patch(**ring), ((0, 1.5, key, 127),), 1.5, [f"pt 0.5 95 {8.5 / 128}", f"pt 1.0 95 {127.5 / 128}"]))
        names.append(f"step k{key}")
    for key in (12, 24, 36):
        J.append((s1.patch(p0=0, p5=0, p14=1), ((0, 1.5, key, 127),), 1.5, []))
        names.append(f"plain sine k{key}")
    J.append((s1.patch(**{**ring, "p0": 1, "p96": 2, "p97": 1}), ((0, 1.5, 48, 127),), 1.5, []))
    names.append("saw + saw sub k48")
    J.append((s1.patch(**{**ring, "p0": 2, "p8": 39, "p96": 1}), ((0, 1.5, 48, 127),), 1.5, []))
    names.append("pulse + tri sub k48")
    return J, names


def fit():
    build()
    lib = ctypes.CDLL(str(LIB))
    P = ctypes.POINTER(ctypes.c_double)
    lib.limiter.argtypes = [P, P, ctypes.c_int] + [ctypes.c_double] * 5
    coef = lambda ms: 1 - np.exp(-1 / (ms * 1e-3 * SR))

    def model(x, q):
        x = np.ascontiguousarray(x)
        y = np.empty_like(x)
        lib.limiter(x.ctypes.data_as(P), y.ctypes.data_as(P), len(x), q[0], q[1], q[2], coef(q[3]), coef(q[4]))
        return y

    J, names = cases()
    Y, X = render(J), render(J, LINEAR)
    sl = slice(int(0.02 * SR), int(1.48 * SR))
    plain = [n.startswith("plain") for n in names]

    def resid(q, idx):
        return np.concatenate([(3.0 if plain[i] else 1.0) * (Y[i][sl] - (1.0 if plain[i] else q[5]) * model(X[i], q)[sl])
                               / np.std(Y[i][sl]) for i in idx])

    idx = range(len(X))
    best = None
    for k in (8, 20, 60):
        for att, rel in ((0.05, 20), (0.5, 10), (2, 50)):
            r = least_squares(lambda q: resid(q, idx), [1.1, 0.9, k, att, rel, 1.0],
                              bounds=([1.0, 0.2, 8, 0.005, 0.5, 0.95], [3, 2, 200, 30, 5000, 1.05]), diff_step=1e-3,
                              max_nfev=150)
            if best is None or r.cost < best.cost:
                best = r
    q = best.x
    print("T %.3f  p %.3f  k %.2f  attack %.3f ms  release %.2f ms  (ring level %.4f)" % tuple(q))
    for i, n in enumerate(names):
        w = 3.0 if plain[i] else 1.0
        print(f"  {n:22s} residual {10 * np.log10(np.mean((resid(q, [i]) / w) ** 2)):6.1f} dB")


def selfosc():
    man = [json.loads(l) for l in open(s1.ROOT / "reference/renders/manifest_filter.jsonl")]
    base = {int(k): v for k, v in next(j for j in man if j["name"] == "t1_r127/c088")["native"].items()}
    cuts = list(range(16, 128, 8)) + [127]
    J = [({**base, 19: c, 20: 127}, ((0, 2.2, 60, 100),), 2.2, []) for c in cuts]
    R, U = render(J), render(J, s1.ROOT / "build/s2render/s2render")

    def line(x):
        f, p = welch(x[int(0.5 * SR):int(2.1 * SR)], SR, nperseg=8192)
        k = np.argmax(p)
        band = (f > f[k] * 0.97) & (f < f[k] * 1.03)
        return np.sqrt(2 * p[band].sum() * (f[1] - f[0])), f[k]

    for c, r, u in zip(cuts, R, U):
        (a, fa), (b, fb) = line(r), line(u)
        print(f"cutoff {c:3d}: original {a:.3f} @ {fa:7.1f} Hz   ours {b:.3f} @ {fb:7.1f} Hz")


if __name__ == "__main__":
    selfosc() if "--selfosc" in sys.argv else fit()
