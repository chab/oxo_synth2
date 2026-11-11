#!/usr/bin/env python3
"""a.d.1 (effect type 0): what ctl1 does, measured.

The original's a.d.1 is out(ctl1) * S(d(ctl1) * x) for ONE fixed system S:
the compression curve of a 784 Hz sine (fundamental gain vs input level,
36 levels from 8e-5 to 0.83) at any ctl1 is the ctl1 64 curve shifted along
log input (drive) and scaled (output gain), within 0.01 dB, and its H2 / H3
profiles collapse the same way. This script fits that shift and scale at
every ctl1 against ctl1 64.

Result (2026-11-12): log drive = 0.063459 per step (70 dB over 0..127, i.e.
d = d64 10^(3.5 (ctl1 - 64) / 127), residual < 0.01 above ctl1 40, < 0.008
below: the estimate, not the law); the output gain is 8.04 at ctl1 0
falling to 1.000 from ~ctl1 48 up (relative to ctl1 64).

-> "ad1_law" in reference/meta/effect.json (gen_tables.py: kAd1OutLaw).
"""
import json

import numpy as np
from scipy import optimize

import s1

SR = 44100.0
KEY = 79
LEVELS = [(1, v) for v in (1, 4, 10, 20, 35, 55, 80, 110)] + [(g, 127) for g in (
    1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 19, 22, 26, 30, 35, 40, 46, 52, 58, 64, 72, 80, 90, 100, 110, 120, 127)]
DRIVE_PER_STEP = 3.5 * np.log(10) / 127


def curves(c1s):
    """per ctl1: [(input amplitude, output fundamental)] (left channel)."""
    f0 = s1.key_hz(KEY)
    specs = [dict(native=s1.patch(p0=0, p5=0, p29=g, p30=127), notes=((0, 0.6, KEY, v),), length=0.6) for g, v in LEVELS]
    for c1 in c1s:
        specs += [dict(native=s1.patch(p0=0, p5=0, p29=g, p30=127, p77=1, p78=0, p79=c1, p80=127, p81=127),
                       notes=((0, 0.6, KEY, v),), length=0.6) for g, v in LEVELS]
    res = s1.render_many(specs)
    amp = lambda a: s1.harmonics(a, SR, f0, 1, start=0.3, dur=0.25)[1][0]
    xin = np.array([amp(a) for a, _ in res[:len(LEVELS)]])
    out = {}
    for i, c1 in enumerate(c1s):
        part = res[len(LEVELS) * (i + 1):len(LEVELS) * (i + 2)]
        out[c1] = (xin, np.array([amp(a) for a, _ in part]))
    return out


def main():
    c1s = list(range(128))
    cv = curves(c1s)
    xr, yr = cv[64]
    lxr, lgr = np.log(xr), np.log(yr / xr)
    log_d, out, misfit = [], [], []
    for c1 in c1s:
        x, y = cv[c1]
        lx, lg = np.log(x), np.log(y / x)

        def r(q):
            a = lx + q[0]
            m = (a > lxr[0]) & (a < lxr[-1])
            return np.where(m, lg - (q[1] + q[0] + np.interp(a, lxr, lgr)), 0)
        law = DRIVE_PER_STEP * (c1 - 64)
        q = optimize.least_squares(r, [law, 0.0]).x
        o = optimize.least_squares(lambda v: r([law, v[0]]), [q[1]]).x[0]  # the output gain with the law's drive
        e = r([law, o])
        log_d.append(float(q[0]))
        out.append(float(np.exp(o)))
        misfit.append(float(np.sqrt(np.mean(e[e != 0] ** 2)) * 20 / np.log(10)))
        print(f"ctl1 {c1:3d}: log drive {q[0]:8.4f} (law {law:8.4f})  out {out[-1]:.5f}"
              f"  misfit with the law {misfit[-1]:.4f} dB", flush=True)
    path = s1.ROOT / "reference/meta/effect.json"
    meta = json.loads(path.read_text())
    meta["ad1_law"] = dict(key=KEY, drive_db_per_step=70.0 / 127, log_drive=log_d, out=out, misfit_db=misfit)
    path.write_text(json.dumps(meta, indent=1))
    print("wrote", path, "| max misfit", max(misfit), "dB")


if __name__ == "__main__":
    main()
