#!/usr/bin/env python3
"""Fit LP24 (type 1) as 4 bilinear one-pole stages with global feedback
through a one-sample delay: H = g * G^4 / (1 + k z^-1 G^4).
Writes reference/meta/filter_ladder.json: fc[res][cut], k[res][cut], g, err."""
import json

import numpy as np
from scipy.optimize import least_squares

import s1

SR = 44100


def ladder(freqs, fc, k, g):
    t = np.tan(np.pi * min(fc, SR * 0.4999) / SR)
    z1 = np.exp(-2j * np.pi * np.asarray(freqs) / SR)
    G = t / (1 + t) * (1 + z1) / (1 - (1 - t) / (1 + t) * z1)
    G4 = G**4
    return g * G4 / (1 + k * z1 * G4)


def fit(h, freqs, x0):
    mag = 20 * np.log10(np.abs(h) + 1e-12)
    keep = (mag > mag.max() - 45) & (freqs > 150) & (freqs < 19500)
    if keep.sum() < 8:
        keep = (mag > mag.max() - 60) & (freqs > 40) & (freqs < 19500)

    def res(p):
        m = ladder(freqs[keep], *p)
        r = np.concatenate([20 * np.log10(np.abs(m) + 1e-12) - mag[keep], 10 * np.angle(m / h[keep])])
        return np.nan_to_num(r, nan=1e3, posinf=1e3, neginf=-1e3)

    best = None
    for kk in (x0[1], x0[1] * 0.7, x0[1] * 0.4, x0[1] * 1.2):
        r = least_squares(res, list(np.clip([x0[0], kk, x0[2]], [6, 0, 0.06], [21900, 4.4, 2.9])), bounds=([5, 0, 0.05], [22000, 4.5, 3]), diff_step=1e-4)
        if best is None or r.cost < best.cost:
            best = r
    e = res(best.x)
    n = keep.sum()
    return best.x, float(np.sqrt(np.mean(e[:n] ** 2)))


def main():
    d = np.load(s1.ROOT / "reference/derived/filter_tf.npz")
    H, F, R = d["H"], d["freqs"], list(d["res"])
    out = {"res": [int(v) for v in R[:-1]], "fc": [], "k": [], "g": [], "rms_err": []}
    for ri, r in enumerate(R[:-1]):  # 127 is nonlinear (self-oscillation), fit separately
        fcs, ks, gs, es = [], [], [], []
        x0 = [17.0, r / 33.0, 1.0]
        for c in range(128):
            p, e = fit(H[1, ri, c], F, x0)
            fcs.append(float(p[0]))
            ks.append(float(p[1]))
            gs.append(float(p[2]))
            es.append(e)
            x0 = [p[0] * 1.06, max(p[1], 0.05) if r else 0.0, p[2]]
        out["fc"].append(fcs)
        out["k"].append(ks)
        out["g"].append(gs)
        out["rms_err"].append(es)
        print(f"res {r}: worst rms {max(es):.2f} dB at c{int(np.argmax(es))}, median {np.median(es):.2f}", flush=True)
    (s1.ROOT / "reference/meta/filter_ladder.json").write_text(json.dumps(out))


if __name__ == "__main__":
    main()
