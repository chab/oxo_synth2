#!/usr/bin/env python3
"""Exact initial osc1 phase of the 32 voice slots, from synced-noise resets.

  measure_slot_osc1.py   -> refines "osc1" in reference/meta/slot_phase.json

Osc2 = noise synced to osc1, mix 127: each osc1 wrap restarts the noise, so
the noise pattern's restart samples ARE osc1's wraps. With the original's
increment (2^-27-truncated, see Voice.hpp PhaseStep) every restart sample
r_k bounds the initial phase: phi0 + r_k inc >= k > phi0 + (r_k - 1) inc.
Two keys (48, 50), 1.5 s per slot (~200 resets each), notes on 16-sample
sub-block starts (a note's oscillators start at its sub-block's start):
phi0 to ~2e-5 cycle, where measure_slot_phase.py's fits were off by up to
2e-4 (enough to move a synced-noise restart by a sample every few cycles).
The filter / output chain is undone with its impulse response, fitted on a
plain noise render (the identical-noise trick).
"""
import json

import numpy as np

import s1

SR = 44100
STEP = 16 * 4300  # samples between notes: whole 16-sample sub-blocks
LEN = 1.5
NS = 32
TAPS = 64


def lfsr(n):
    s = [0] * 16 + [1, 0, 1, 0]
    for _ in range(n):
        s.append(s[-13] ^ s[-18] ^ s[-19] ^ s[-20])
    return np.array(s)


def chain_ir():
    (x, _), = s1.render_many([dict(native=s1.patch(p1=4, p5=127, p91=1), notes=((0, 0.5, 48, 127),), length=0.5)])
    u = np.where(lfsr(15000)[:15000] == 1, 0.6504, -0.6504)
    U = np.stack([u[2000 - k:12000 - k] for k in range(TAPS)], 1)
    return np.linalg.lstsq(U, x[0][2000:12000], rcond=None)[0]


def signs(x, h):
    n = 1 << int(np.ceil(np.log2(len(x))))
    H = np.fft.rfft(h, n)
    return np.sign(np.fft.irfft(np.fft.rfft(x, n) * np.conj(H) / (np.abs(H) ** 2 + 1e-5), n)[:len(x)])


def bounds(key, h):
    inc = np.floor(s1.key_hz(key) / SR * 2**27) / 2**27
    (y, _), = s1.render_many([dict(native=s1.patch(p0=1, p1=4, p5=127, p6=1, p91=1), notes=((0, 1, key, 127),),
                                   length=1)])
    first = int(np.ceil(2 / inc))
    pattern = signs(y[0], h)[first:first + 60]
    notes = tuple((STEP * i / SR, STEP * i / SR + LEN, key, 127) for i in range(NS))
    (x, _), = s1.render_many([dict(native=s1.patch(p0=1, p1=4, p5=127, p6=1, p91=0, p94=NS), notes=notes,
                                   length=STEP * NS / SR + 0.1)])
    out = []
    for i in range(NS):
        seg = signs(x[0][STEP * i:STEP * i + int(LEN * SR)], h)
        # (the noise also starts from its seed at the note on: not a wrap)
        starts = [r for r in np.where(np.correlate(seg, pattern, "valid") >= 58)[0] if r > 0]
        lo, hi = -1e9, 1e9
        for k, r in enumerate(starts, start=1):
            lo, hi = max(lo, k - r * inc), min(hi, k - (r - 1) * inc)
        out.append((lo, hi))
    return out


def main():
    h = chain_ir()
    a, b = bounds(48, h), bounds(50, h)
    path = s1.ROOT / "reference/meta/slot_phase.json"
    meta = json.loads(path.read_text())
    old = meta["osc1"]
    new = []
    for i, ((a0, a1), (b0, b1)) in enumerate(zip(a, b)):
        b0, b1 = b0 + np.round(a0 - b0), b1 + np.round(a0 - b0)
        phi = float(np.mean([a0, a1, b0, b1]) % 1)
        new.append(round(phi, 8))
        print(f"slot {i:2d} {phi:.6f}  spread {max(a1, b1) - min(a0, b0):.1e}  (fit {old[i]:.6f})")
    meta["osc1"] = new
    path.write_text(json.dumps(meta, indent=1) + "\n")


if __name__ == "__main__":
    main()
