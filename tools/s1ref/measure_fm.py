#!/usr/bin/env python3
"""FM index table: osc1 sine carrier, osc2 triangle modulator at +7 semis.
Model (fits to ~1e-8): inst_freq1 = f1 * (1 + beta * tri2), tri2 in [-1, 1].
Writes reference/meta/fm.json {beta[v]}."""
import json
import numpy as np
from scipy.optimize import minimize_scalar
import s1

SR = 44100
K = 60
F1 = s1.key_hz(K)
F2 = F1 * 2 ** (7 / 12)
N = int(0.5 * SR)
t = np.arange(N) / SR
KS = range(-6, 7)


def lines(sig):
    fr = sorted({(k, abs(F1 + k * F2)) for k in KS if 0 < abs(F1 + k * F2) < SR / 2}, key=lambda q: q[1])
    cols = [np.ones_like(t)]
    for _, f in fr:
        cols += [np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)]
    c, *_ = np.linalg.lstsq(np.stack(cols, 1), sig, rcond=None)
    d = {}
    for i, (k, _) in enumerate(fr):
        d[k] = d.get(k, 0) + np.hypot(c[1 + 2 * i], c[2 + 2 * i])
    return np.array([d.get(k, 0) for k in KS])


def tri(ph):
    return 1 - 4 * np.abs((ph % 1.0) - 0.5)


def model(beta):
    inst = F1 * (1 + beta * tri(F2 * t))
    return 0.2 * np.sin(2 * np.pi * np.cumsum(inst) / SR)


def main():
    vals = list(range(128))
    outs = s1.render_many([dict(native=s1.patch(p0=0, p1=3, p2=71, p45=v), notes=((0, 0.9, K, 127),), length=0.85)
                           for v in vals])
    betas, errs = [], []
    prev = 0.0
    for v, (x, _) in zip(vals, outs):
        ref = lines(np.asarray(x[0][int(0.3 * SR) : int(0.3 * SR) + N], float))
        f = lambda b: float(np.sum((lines(model(b)) - ref) ** 2))
        # the error oscillates in beta at large indices: a plain bounded
        # search fell into a wrong minimum from 124 up (83.6 instead of
        # 71.6), so take the best of a grid, then refine around it
        lo, hi = max(0.0, prev * 0.98 - 0.01), prev * 1.1 + 0.05
        grid = np.linspace(lo, hi, 201)
        g = grid[int(np.argmin([f(b) for b in grid]))]
        step = (hi - lo) / 200
        r = minimize_scalar(f, bounds=(max(lo, g - step), min(hi, g + step)), method="bounded", options={"xatol": 1e-5})
        betas.append(float(r.x))
        errs.append(r.fun)
        prev = r.x
    path = s1.ROOT / "reference/meta/fm.json"
    path.write_text(json.dumps({"beta": betas, "fit_err": errs}, indent=1))
    print([(v, round(betas[v], 4), "%.1e" % errs[v]) for v in range(0, 128, 8)], betas[127])


if __name__ == "__main__":
    main()
