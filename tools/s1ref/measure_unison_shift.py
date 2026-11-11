#!/usr/bin/env python3
"""What the UNISON PHASE knob (92) shifts, per oscillator, original vs ours.

  measure_unison_shift.py

2 stacked voices at +-5.1 cents (unison detune 32), osc phase 64 (coherent
starts), key 81. Per unison phase value, least squares over 2.9 s gives the
start phase of every partial; printed: voice 1 minus voice 0, in degrees.
  osc1   sine, alone
  copies sine + osc1 detune 127 (main + 8 copies at +-20..140 cents)
  osc2   triangle (fundamental only), alone, osc2 pitch 0 / +12

Result (original, 2026-11-11): every oscillator of stack voice k is shifted
by (its slot table's phase at slot k - at slot 0) * v / 127, the difference
taken RAW (unwrapped, -1..1 cycle): osc1 kSlotOsc1Phase (= kPhaseDeg), osc2
kSlotOsc2Phase (+106 degrees for k = 1, in osc2's own cycle at any osc2
pitch), each detune copy kSlotCopyPhase (-209..+185 degrees for k = 1).
"""
import re

import numpy as np

import s1

SR, KEY = 44100, 81
F0 = 440 * 2 ** ((KEY - 69) / 12)
D, d = 5.09714, 20.0  # kUnisonDetuneCents[32], osc1 detune 127
OFFS = [0, -7, -5, -3, -1, 1, 3, 5, 7]
UPH = [0, 32, 64, 96, 127]


def fit(x, freqs):
    t = np.arange(int(0.05 * SR), int(2.95 * SR)) / SR
    m = np.concatenate([[np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)] for f in freqs]).T
    c = np.linalg.lstsq(m, x[int(0.05 * SR): int(0.05 * SR) + len(t)], rcond=None)[0]
    return np.degrees(np.arctan2(-c[1::2], c[0::2]))


def cases():
    base = dict(p73=1, p93=2, p75=32, p84=64, p91=64)
    yield "osc1", s1.patch(p0=0, **base), [F0 * 2 ** ((2 * j - 1) * D / 1200) for j in (0, 1)], 1
    yield "copies", s1.patch(p0=0, p76=127, **base), [F0 * 2 ** (((2 * j - 1) * D + o * d) / 1200) for j in (0, 1) for o in OFFS], 9
    hpp = (s1.ROOT / "EngineCore/MeasuredTables.hpp").read_text()
    table = [int(v) for v in re.search(r"kOsc2PitchSemis\[128\] = \{([^}]*)\}", hpp).group(1).replace(",", " ").split()]
    for semis in (0, 12):
        f2 = F0 * 2 ** (semis / 12)
        yield f"osc2 {semis:+d}", s1.patch(p1=3, p2=table.index(semis), p5=127, **base), [f2 * 2 ** ((2 * j - 1) * D / 1200) for j in (0, 1)], 1


def main():
    for name, patch, freqs, n in cases():
        specs = [{"native": {**patch, 92: u}, "notes": ((0.0, 3.0, KEY, 127),), "length": 3.0} for u in UPH]
        for ours in (False, True):
            rows = []
            for (x, _), u in zip(s1.render_many(specs, ours=ours), UPH):
                p = fit(np.asarray(x)[0] if np.asarray(x).shape[0] == 2 else np.asarray(x)[:, 0], freqs)
                rows.append((p[n:] - p[:n]) % 360)
            print(f"{name:8s} {'s2' if ours else 's1'}: " + " | ".join(
                f"u{u}: " + " ".join(f"{v:3.0f}" for v in r) for u, r in zip(UPH, rows)))


if __name__ == "__main__":
    main()
