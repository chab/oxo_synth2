#!/usr/bin/env python3
"""FM on every oscillator pair, original vs ours: osc1 shape x osc2 shape x
FM knob, osc1 only in the mix (FM shapes osc1), neutral dry patch. --ring:
ring on, osc2 only in the mix (= osc2 x the FM'd osc1).

  check_fm.py [--sync] [--ring] [--key=K] [--pitch=P] [--fms=0,10,...] [--level]

Per cell: spec = mean |third-octave difference| (dB, compare.score), or the
level difference with --level. One note per render (1 s, key K, default 48).
"""
import sys

import numpy as np

import compare
import s1

OSC1 = {0: "sine", 1: "saw", 2: "pulse", 3: "tri"}
OSC2 = {1: "saw", 2: "pulse", 3: "tri", 4: "noise"}


def opt(name, default, cast=int):
    for a in sys.argv[1:]:
        if a.startswith(f"--{name}="):
            return cast(a.split("=", 1)[1])
    return default


def main():
    sync, ring = "--sync" in sys.argv, "--ring" in sys.argv
    key, pitch = opt("key", 48), opt("pitch", 64)
    fms = opt("fms", [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 127],
              lambda s: [int(v) for v in s.split(",")])
    col = 0 if "--level" in sys.argv else 1
    pairs = [(a, b) for a in OSC1 for b in OSC2]
    specs = [dict(native=s1.patch(p0=a, p1=b, p2=pitch, p5=127 if ring else 0, p6=int(sync), p7=int(ring), p45=v), notes=((0, 1.0, key, 127),),
                  length=1.2) for a, b in pairs for v in fms]
    refs, ours = s1.render_many(specs), s1.render_many(specs, ours=True)
    what = "level (ours - ref)" if col == 0 else "spec"
    print(f"{what} dB | key {key} osc2 pitch {pitch - 64:+d} sync {'on' if sync else 'off'} ring {'on' if ring else 'off'}")
    print(f"{'osc1/osc2':12s}" + "".join(f"{v:6d}" for v in fms))
    i = 0
    for a, b in pairs:
        row = []
        for _ in fms:
            (r, sr), (u, _) = refs[i], ours[i]
            row.append(compare.score(r, u, sr)[col])
            i += 1
        print(f"{OSC1[a] + '/' + OSC2[b]:12s}" + "".join(f"{x:6.1f}" if abs(x) >= 1 else f"{x:6.2f}" for x in row))


if __name__ == "__main__":
    main()
