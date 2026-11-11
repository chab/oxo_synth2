#!/usr/bin/env python3
"""Pitch modulation resolution.

  measure_pitch_mod.py

Held pitch bends (sine, pitch tracked): only the 7-bit MSB counts, s =
trunc((v - 8192) / 128) clamped to +-63, cents = s / 63 * range, then
1/128-octave steps (9.375 cents) TRUNCATED toward 0 (0 mismatches over 178
held bends at ranges 2 and 12). An LFO on pitch plays whole steps of its
own, added to the bend's (laws per waveform: measure_lfo_pitch.py), so a
slow LFO vibrato is a staircase; fine tune and the mod env's pitch are not
quantized (fractional histograms below). Voice.hpp PitchRatio, Lfo.hpp.
"""
import numpy as np

import instfreq
import s1

SR = 44100
STEP = 1200 / 128


def track(spec, a, b):
    (x, _), = s1.render_many([spec])
    return instfreq.inst_cents(x[:1], SR, 440.0, 0.004)[int(a * SR):int(b * SR)]


def main():
    vals = list(range(8192, 16384, 683)) + [16383] + list(range(8192, 0, -751)) + [0]
    bends = tuple((0.1 + i * 0.2, v) for i, v in enumerate(vals))
    length = 0.2 + len(vals) * 0.2
    c = track({"native": s1.patch(p0=0, p91=1, p40=12), "notes": ((0, length, 69, 127),), "length": length,
               "bends": bends}, 0, length)
    print("bend (range 12): 7-bit cents -> measured -> trunc model")
    for i, v in enumerate(vals):
        cont = max(-63, min(63, int((v - 8192) / 128))) / 63 * 1200
        m = np.median(c[int((0.15 + i * 0.2) * SR):int((0.28 + i * 0.2) * SR)])
        print(f"  {cont:9.2f} -> {m:9.2f}   {STEP * np.trunc(cont / STEP):9.2f}")
    cases = (("slow triangle LFO", s1.patch(p0=0, p91=1, p57=1, p41=2, p42=1, p43=20, p44=64, p68=1)),
             ("LFO + fine tune 70", s1.patch(p0=0, p91=1, p57=1, p41=2, p42=1, p43=20, p44=64, p68=1, p72=70)),
             ("mod env -> osc2", s1.patch(p1=3, p5=127, p91=1, p10=1, p11=70, p12=0, p13=90, p71=0)))
    for name, p in cases:
        c = track({"native": p, "notes": ((0, 4.0, 69, 127),), "length": 4.0}, 0.2, 3.9)
        print(f"{name:20s} fractional steps histogram", np.histogram((c / STEP) % 1, bins=10, range=(0, 1))[0])


if __name__ == "__main__":
    main()
