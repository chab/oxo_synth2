#!/usr/bin/env python3
"""Initial oscillator phases of the 32 voice slots (osc phase 0 = free).

  measure_slot_phase.py   -> reference/meta/slot_phase.json

With osc phase 0 the original never resets a voice slot's oscillators: each
slot keeps its osc1 / osc2 phase across notes (frozen while idle, see
check_unison.py's history cases) from values set at instance creation (first
instance of a process). Four 8-voice unison notes (detune 127: partials
distinct) walk slots 0..31 in round robin; a least squares fit gives each
slot's fundamental phase at its note on (sine osc1; osc2 = triangle alone,
fundamental in phase with a sine), referenced to the same render at osc
phase 1 (osc1 = osc2 = phase 0). Slots 1..7 relative to slot 0 = kPhaseDeg.
Osc1 detune copies: 32 single notes (one per slot), osc1 detune 127 (copies
at +-20, 60, 100, 140 cents, all partials distinct), referenced to osc
phase 1 (copies start with osc1 there, kCopyPhaseDeg * 0).
"""
import json

import numpy as np

import check_unison
import s1

SR, KEY, NV = 44100, 81, 8
F0 = 440 * 2 ** ((KEY - 69) / 12)
D = 49.98083  # kUnisonDetuneCents[127]
FREQS = [F0 * 2 ** ((2 * i / (NV - 1) - 1) * D / 1200) for i in range(NV)]
NOTES = check_unison.grid(tuple((1.2 * i, 1.2 * i + 1.0, KEY, 127) for i in range(4)))


def slot_phases(extra):
    p = s1.patch(p0=0, p73=1, p93=NV, p75=127, p84=64, p94=32)
    p.update(extra)
    (x, _), = s1.render_many([{"native": p, "notes": NOTES, "length": 4.8}])
    out = []
    for on, *_ in NOTES:
        i0 = round(on * SR)
        n = np.arange(i0 + int(0.02 * SR), i0 + int(0.98 * SR))
        t = (n - i0) / SR
        m = np.concatenate([[np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)] for f in FREQS]).T
        c = np.linalg.lstsq(m, x[0][n], rcond=None)[0]
        out += list(np.degrees(np.arctan2(c[1::2], c[0::2])))
    return np.array(out)


COPY_NOTES = check_unison.grid(tuple((0.6 * i, 0.6 * i + 0.5, KEY, 127) for i in range(32)))
COPY_FREQS = [F0 * 2 ** (o * 20 / 1200) for o in (0, -7, -5, -3, -1, 1, 3, 5, 7)]  # main, kDetuneOffsets


def copy_phases(extra):
    p = s1.patch(p0=0, p76=127, p94=32)
    p.update(extra)
    (x, _), = s1.render_many([{"native": p, "notes": COPY_NOTES, "length": 0.6 * 32}])
    out = []
    for on, *_ in COPY_NOTES:
        i0 = round(on * SR)
        n = np.arange(i0 + int(0.01 * SR), i0 + int(0.49 * SR))
        t = (n - i0) / SR
        m = np.concatenate([[np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)] for f in COPY_FREQS]).T
        c = np.linalg.lstsq(m, x[0][n], rcond=None)[0]
        out.append(np.degrees(np.arctan2(c[1::2], c[0::2])))
    return np.array(out)


def main():
    res = {}
    for name, extra in (("osc1", {}), ("osc2", {5: 127, 1: 3})):
        free, zero = slot_phases({**extra, 91: 0}), slot_phases({**extra, 91: 1})
        res[name] = [round(float(v), 8) for v in ((zero - free) / 360) % 1]  # cycles; phase lags the fit angle
        print(name, " ".join(f"{v:.3f}" for v in res[name]))
    free, zero = copy_phases({91: 0}), copy_phases({91: 1})
    cyc = ((zero - free) / 360) % 1  # [slot][main, copies -7..7]
    print("osc1 (single notes) vs unison walk, max |diff| (cycles):",
          f"{np.abs((cyc[:, 0] - np.array(res['osc1']) + 0.5) % 1 - 0.5).max():.3f}")
    res["copies"] = [[round(float(v), 8) for v in row[1:]] for row in cyc]
    for i, row in enumerate(res["copies"]):
        print(f"slot {i:2d} copies", " ".join(f"{v:.3f}" for v in row))
    (s1.ROOT / "reference/meta/slot_phase.json").write_text(json.dumps(res, indent=1) + "\n")


if __name__ == "__main__":
    main()
