#!/usr/bin/env python3
"""Effect section probes, original vs ours. Input = the same render with
the effect off (fresh processes are deterministic), so y vs x is exact.
Helpers are imported by the per-type investigation scripts."""
import numpy as np
from scipy import signal

import s1

SR = 44100


def fx(t, c1, c2, level=127):
    return dict(p77=1, p78=t, p79=c1, p80=c2, p81=level)


def pair(kw, base, notes=((0, 1.0, 33, 127),), length=1.0, ours=False):
    """x (effect off), y (effect on), left channel, as float arrays."""
    specs = [dict(native=s1.patch(**base, **kw), notes=notes, length=length),
             dict(native=s1.patch(**base), notes=notes, length=length)]
    (y, _), (x, _) = s1.render_many(specs, ours=ours)
    return np.asarray(x[0], float), np.asarray(y[0], float)


def many(kws, base, notes=((0, 1.0, 33, 127),), length=1.0, ours=False):
    specs = [dict(native=s1.patch(**base, **kw), notes=notes, length=length) for kw in kws]
    specs.append(dict(native=s1.patch(**base), notes=notes, length=length))
    outs = [np.asarray(o[0], float) for o, _ in s1.render_many(specs, ours=ours)]
    return outs[-1], outs[:-1]


def curve(x, y, bins=41, a=0.2, b=0.9):
    """Static transfer estimate: median y per x bin (steady part)."""
    n = len(x)
    xs, ys = x[int(a * n):int(b * n)], y[int(a * n):int(b * n)]
    edges = np.linspace(xs.min(), xs.max(), bins + 1)
    idx = np.clip(np.digitize(xs, edges) - 1, 0, bins - 1)
    cx = np.array([np.median(xs[idx == i]) if np.any(idx == i) else np.nan for i in range(bins)])
    cy = np.array([np.median(ys[idx == i]) if np.any(idx == i) else np.nan for i in range(bins)])
    resid = ys - np.interp(xs, cx[~np.isnan(cx)], cy[~np.isnan(cx)])
    return cx, cy, float(np.sqrt(np.mean(resid ** 2)) / (np.sqrt(np.mean(ys ** 2)) + 1e-12))


def transfer(x, y, nper=8192):
    f, pxy = signal.csd(x, y, SR, nperseg=nper)
    _, pxx = signal.welch(x, SR, nperseg=nper)
    return f, pxy / pxx


SINE = dict(p0=0, p5=0)
NOISE = dict(p0=2, p8=0, p5=127, p1=4)


def rbj(fc, q, kind, sr=SR):
    w = 2 * np.pi * fc / sr
    al, c = np.sin(w) / (2 * q), np.cos(w)
    if kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
    else:
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    a = [1 + al, -2 * c, 1 - al]
    return np.array(b) / a[0], np.array(a) / a[0]


def lp_hz(c2):
    return 400.0 * 40.0 ** (c2 / 127.0)


def unfilter(x, y, c2, hp=(100.0, 2.29)):
    """HP-filtered input and LP-inverted output (types 1 / 2 linear frame)."""
    bh, ah = rbj(hp[0], hp[1], "hp")
    bl, al = rbj(lp_hz(c2), 3.0, "lp")
    return signal.lfilter(bh, ah, x), signal.lfilter(al, bl, y)


def fit_nl(x, y, c2, hp=(100.0, 2.29), knots=41, umax=None, a=0.2, b=0.9):
    """Piecewise-linear f with y = LP(f(HP(x))): linear least squares.
    Returns (u knots, f values, relative rms residual)."""
    bh, ah = rbj(hp[0], hp[1], "hp")
    bl, al = rbj(lp_hz(c2), 3.0, "lp")
    u = signal.lfilter(bh, ah, x) if hp else x
    n = len(x)
    s = slice(int(a * n), int(b * n))
    umax = umax or float(np.abs(u[s]).max())
    k = np.linspace(-umax, umax, knots)
    cols = []
    for i in range(knots):
        phi = np.interp(u, k, np.eye(knots)[i])
        cols.append(signal.lfilter(bl, al, phi)[s])
    A = np.stack(cols, 1)
    c, *_ = np.linalg.lstsq(A, y[s], rcond=None)
    r = y[s] - A @ c
    return k, c, float(np.sqrt(np.mean(r ** 2)) / (np.sqrt(np.mean(y[s] ** 2)) + 1e-12))


def fit_nl_post(x, y, c2, hp=(100.0, 2.29), knots=41, a=0.2, b=0.9, gain_hp=1.0):
    """Piecewise-linear f with y = LP(HP(f(x))) (types 1 / 2)."""
    bh, ah = rbj(hp[0], hp[1], "hp")
    bl, al = rbj(lp_hz(c2), 3.0, "lp")
    n = len(x)
    s = slice(int(a * n), int(b * n))
    umax = float(np.abs(x[s]).max())
    k = np.linspace(-umax, umax, knots)
    cols = []
    for i in range(knots):
        phi = np.interp(x, k, np.eye(knots)[i])
        cols.append(signal.lfilter(bl, al, signal.lfilter(bh, ah, phi))[s])
    A = np.stack(cols, 1)
    c, *_ = np.linalg.lstsq(A, y[s], rcond=None)
    r = y[s] - A @ c
    c = c - np.interp(0, k, c)  # the HP removes DC: f is known up to an offset
    return k, c, float(np.sqrt(np.mean(r ** 2)) / (np.sqrt(np.mean(y[s] ** 2)) + 1e-12))


DRIVE_C1 = list(range(0, 128, 4)) + [127]
AD2_U = np.linspace(0, 10, 201)


def drive_tables():
    """Small-signal gain |H(1 kHz)| per ctl1 for types 0..2 (tiny noise;
    the high drives at a lower input level so nothing saturates)."""
    res = {}
    for t in (0, 1, 2):
        vals = {}
        for lo, hi, p29 in ((0, 72, 8), (72, 128, 2)):
            cs = [c for c in DRIVE_C1 if lo <= c < hi]
            x, ys = many([fx(t, c, 127) for c in cs], dict(NOISE, p29=p29, p30=127), notes=((0, 2.0, 60, 1),), length=2.0)
            for c, y in zip(cs, ys):
                f, H = transfer(x[4410:], y[4410:], nper=8192)
                vals[c] = float(abs(H[int(np.argmin(np.abs(f - 1000)))]))
        res[t] = [vals[c] for c in DRIVE_C1]
    return res


def ad2_curve(drive):
    """a.d.2 transfer F(u = drive * x): odd part of the piecewise-linear fits
    of several drives (y = LP(HP(F(x)))), median-combined."""
    C = (24, 40, 56, 80)
    x, ys = many([fx(1, c, 127) for c in C], dict(SINE, p29=64), notes=((0, 1.0, 33, 127),), length=1.0)
    rows = []
    for c, y in zip(C, ys):
        k, f, _ = fit_nl_post(x, y, 127, knots=201)
        u = k * drive[DRIVE_C1.index(c)]
        fo = 0.5 * (np.interp(AD2_U, u, f) - np.interp(-AD2_U, u, f))
        rows.append(np.where(AD2_U <= u.max() * 0.98, fo, np.nan))
    F = np.nanmedian(np.array(rows), 0)
    return np.where(np.isnan(F), np.nanmax(F), F).tolist()


def main():
    import json
    d = drive_tables()
    path = s1.ROOT / "reference/meta/effect.json"
    out = json.loads(path.read_text()) if path.exists() else {}  # keeps "ad1" (fit_ad1.py), "dd_*" (measure_dd.py)
    out.update({"c1": DRIVE_C1, "drive": {str(t): v for t, v in d.items()}, "ad2_u": AD2_U.tolist(),
                "ad2_curve": ad2_curve(d[1]), "ad2": {"hp_hz": 100.0, "hp_q": 2.29, "lp_q": 3.0}})
    path.write_text(json.dumps(out, indent=1))
    print("wrote reference/meta/effect.json")


if __name__ == "__main__":
    main()
