#!/usr/bin/env python3
"""Fit the 2x-iterated Chamberlin SVF (coefficients f, d) to the measured
transfer functions of filter types 0 (LP12), 2 (HP12), 3 (BP12).

Closed form of two SVF iterations with the input held per sample, then a
least-squares fit on log magnitude + phase above 150 Hz.
Writes reference/meta/filter_svf.json: f[type][res][cut], d[...], err[...].
"""
import json

import numpy as np
from scipy.optimize import least_squares

import s1

SR = 44100


def svf2(freqs, f, d, kind):
    """Two Chamberlin iterations per sample, input held: state-space closed form."""
    # state s = [low, band]; one iteration: low' = low + f*band
    #   high = x - low' - d*band ; band' = f*high + band
    A1 = np.array([[1.0, f], [-f, 1 - f * f - f * d]])
    B1 = np.array([0.0, f])
    A = A1 @ A1
    B = A1 @ B1 + B1
    # output after the 2nd iteration, from state before it (s1) and x:
    # low = s1.low + f*s1.band ; band = f*(x - low - d*s1.band) + s1.band ; high = x - low - d*s1.band
    z = np.exp(-2j * np.pi * np.asarray(freqs) / SR)
    # s = z (I - z A)^-1 B  (state before the sample), 2x2 inverse in closed form
    m00, m01, m10, m11 = 1 - z * A[0, 0], -z * A[0, 1], -z * A[1, 0], 1 - z * A[1, 1]
    det = m00 * m11 - m01 * m10
    s0 = z * (m11 * B[0] - m01 * B[1]) / det
    s1 = z * (-m10 * B[0] + m00 * B[1]) / det
    # first iteration of the sample, then the output iteration
    l1 = s0 + f * s1
    b1 = f * (1.0 - l1 - d * s1) + s1
    low = l1 + f * b1
    high = 1.0 - low - d * b1
    band = f * high + b1
    return {"lp": low, "hp": high, "bp": band}[kind]


def fit(h, freqs, kind, x0):
    mag = 20 * np.log10(np.abs(h) + 1e-12)
    keep = (mag > mag.max() - 45) & (freqs > 150) & (freqs < 19500)

    def res(p):
        m = svf2(freqs[keep], p[0], p[1], kind)
        r = np.concatenate([20 * np.log10(np.abs(m) + 1e-12) - mag[keep], 10 * np.angle(m / h[keep])])
        return np.nan_to_num(r, nan=1e3, posinf=1e3, neginf=-1e3)

    r = least_squares(res, x0, bounds=([1e-6, 0.0], [2.2, 3.0]), diff_step=1e-4)
    e = res(r.x)
    n = keep.sum()
    return r.x, float(np.abs(e[:n]).max()), float(np.degrees(np.abs(e[n:] / 10).max()))


def main():
    d = np.load(s1.ROOT / "reference/derived/filter_tf.npz")
    H, F, RES = d["H"], d["freqs"], d["res"]
    out = {"res": RES.tolist(), "f": {}, "d": {}, "err_db": {}, "err_deg": {}}
    for t, kind in ((0, "lp"), (2, "hp"), (3, "bp")):
        fs, ds, eds, eps = [], [], [], []
        for ri in range(len(RES)):
            row_f, row_d, row_e, row_p = [], [], [], []
            x0 = [0.01, 1.0]
            for c in range(128):
                p, ed, ep = fit(H[t, ri, c], F, kind, x0)
                row_f.append(float(p[0]))
                row_d.append(float(p[1]))
                row_e.append(ed)
                row_p.append(ep)
                x0 = [min(2.1, p[0] * 1.07), p[1]]
            fs.append(row_f)
            ds.append(row_d)
            eds.append(row_e)
            eps.append(row_p)
            print(f"type {t} res {RES[ri]}: worst {max(row_e):.2f} dB, median {np.median(row_e):.2f} dB", flush=True)
        out["f"][t], out["d"][t], out["err_db"][t], out["err_deg"][t] = fs, ds, eds, eps
    (s1.ROOT / "reference/meta/filter_svf.json").write_text(json.dumps(out))


if __name__ == "__main__":
    main()
