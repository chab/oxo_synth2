#!/usr/bin/env python3
"""How an LFO on pitch is quantized: the original next to ours (s2render).

Pitch plays in 1/128-octave steps (9.375 cents, measure_pitch_mod.py). An
LFO on pitch plays whole steps of ITS OWN, N = the square LFO's steps at
that depth (kLfoPitchCents / 9.375, whole numbers), with a law per waveform
(v = the LFO value, -1..1), the same at every speed and depth:

  saw       2N + 1 equal bands of v: floor((v + 1) / 2 * (2N + 1)) - N
  triangle  rising floor(N v), falling ceil(N v): a step is taken when the
            wave reaches it (so depth 1 sits on -1 until the wave crosses 0)
  square    +-N
  sine      sign(v) floor(N |v| + 1/4)
  S&H, smooth random  floor(N v): -N .. N - 1 (never +N)

The bend's own truncated steps and both LFOs' steps add (a bend between two
steps does not move the LFO's thresholds). osc1+2 and osc2-only alike.

Probe: LFO1 on pitch, LFO2 the same type / speed / key sync on amp (depth
64, a linear law): the amp envelope gives the LFO value at every sample, the
pitch (osc1 sine, key 84) the step it plays; printed = the LFO value at each
step change, "^" rising, "v" falling. Random types: their two LFOs (and two
renders) hold different values, so only the set of steps is compared,
after the first cycles: the original's first smooth-random cycle after the
instance starts ramps down from ~+13 steps whatever the depth (not modelled).

  measure_lfo_pitch.py"""
import collections

import numpy as np
from scipy.signal import hilbert

import instfreq
import s1

SR = 44100
STEP = 9.375
KEY = 84


def render(typ, depth, ours, amp=True, speed=20, length=8.0, extra=None):
    p = {**s1.DRY, 0: 0, 5: 0, 57: 1, 41: 2, 42: typ, 43: speed, 44: depth, 68: 1, 67: 0,
         58: 1 if amp else 0, 46: 4, 47: typ, 48: speed, 49: 64, 70: 1, 69: 0, **(extra or {})}
    x = s1.render_many([dict(native=p, notes=((0.0, length - 0.1, KEY, 100),), length=length)], ours=ours)[0][0]
    a, b = int(0.2 * SR), int((length - 0.3) * SR)
    env = np.convolve(np.abs(hilbert(x[0].astype(np.float64))), np.ones(221) / 221, "same")[a:b]
    steps = instfreq.inst_cents(x, SR, s1.key_hz(KEY), smooth=0.002)[a:b] / STEP
    lo, hi = np.percentile(env, 0.05), np.percentile(env, 99.95)
    return 2 * (env - lo) / (hi - lo) - 1, steps


def changes(lfo, steps):
    """median LFO value at each change between two steps, per (lower step, rising)"""
    level = np.rint(steps).astype(int)
    runs, i = [], 0
    while i < len(level):
        j = i
        while j < len(level) and level[j] == level[i]:
            j += 1
        if j - i > 0.02 * SR:
            runs.append((level[i], i, j))
        i = j
    out = collections.defaultdict(list)
    for (s0, _, j0), (s1_, i1, _) in zip(runs, runs[1:]):
        if abs(s1_ - s0) == 1:
            out[(min(s0, s1_), s1_ > s0)].append(lfo[(j0 + i1) // 2])
    return " ".join(f"{k:+d}{'^' if up else 'v'}{k + 1:+d}@{np.median(v):+.3f}" for (k, up), v in sorted(out.items()))


def main():
    for typ, name in ((0, "saw"), (1, "triangle"), (5, "sine")):
        for depth in (1, 2, 3):
            for ours in (False, True):
                print(f"{name:8s} depth {depth} {'ours    ' if ours else 'original'}: {changes(*render(typ, depth, ours))}")
    for typ, name in ((3, "S&H"), (4, "smooth")):
        for depth in (1, 3):
            for ours in (False, True):
                steps = render(typ, depth, ours, amp=False, speed=66 if typ == 3 else 40, length=20.0)[1][int(3 * SR):]
                seen = collections.Counter(np.rint(steps).astype(int).tolist())
                print(f"{name:8s} depth {depth} {'ours    ' if ours else 'original'}: steps "
                      f"{sorted(k for k, n in seen.items() if n > 0.02 * SR)}")


if __name__ == "__main__":
    main()
