#!/usr/bin/env python3
"""Random LFOs (3 S&H, 4 smooth) under KEY SYNC: what does a restart do to
the random sequence? LFO1 on amp at depth 127 (gain = (lfo + 1) / 2), osc1
sine at key 84, so the output envelope IS the LFO. A train of separate notes
(no key held between them = every note restarts the LFO). Prints the LFO
value read from the envelope (8 ms windows) per note, original vs ours.
Usage: measure_lfo_random_keysync.py [speed] [on_s] [gap_s] [notes]"""
import sys

import numpy as np

import s1

SR = 44100
F = s1.key_hz(84)


def lfo_curve(x, ref, t0, t1, win=0.008):
    out = []
    for t in np.arange(t0, t1 - win, win):
        out.append(2 * s1.tone_amp(x, SR, F, start=t, dur=win) / ref - 1)
    return np.array(out)


def main():
    speed = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    on = float(sys.argv[2]) if len(sys.argv) > 2 else 0.3
    gap = float(sys.argv[3]) if len(sys.argv) > 3 else 0.1
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    notes = tuple((i * (on + gap), i * (on + gap) + on, 84, 127) for i in range(count))
    length = count * (on + gap) + 0.2
    specs = [dict(native=s1.patch(p0=0, p57=1, p41=4, p42=t, p43=speed, p44=d, p68=ks), notes=notes, length=length)
             for t in (3, 4) for ks in (1, 0) for d in (0, 127)]
    for ours in (False, True):
        outs = s1.render_many(specs, ours=ours)
        print("ours" if ours else "SYNTH1")
        for k, (t, ks) in enumerate((t, ks) for t in (3, 4) for ks in (1, 0)):
            ref, x = outs[2 * k][0], outs[2 * k + 1][0]
            print(f"  type {t} keysync {ks}")
            for i, (a, b, _, _) in enumerate(notes):
                r = s1.tone_amp(ref, SR, F, start=a + 0.03, dur=on - 0.06)
                c = lfo_curve(x, r, a + 0.03, b - 0.02)
                sel = c[:: max(1, len(c) // 6)]
                print(f"    note {i}: " + " ".join(f"{v:+.3f}" for v in sel))


if __name__ == "__main__":
    main()
