#!/usr/bin/env python3
"""Fit LPDL (filter type 4, diode ladder) on the measured transfer functions
(reference/derived/filter_tf.npz, measure_filter.py): linear 303-style coupled
ladder (tridiagonal -2/1 stages, last -1) whose feedback is high-passed at
100 Hz; per (res, cutoff step 4) the stage rate w (rad/s), feedback k and
output gain g; output = (y3 + y4) / 2. Magnitude fit, ~0.5 dB mean. Writes reference/meta/filter_lpdl.json."""
import numpy as np, json
from scipy.optimize import least_squares
import s1
import sys
SRC = sys.argv[sys.argv.index("--in") + 1] if "--in" in sys.argv else "filter_tf.npz"
d = np.load(s1.ROOT / "reference/derived" / SRC); H, F, R = d["H"], d["freqs"], list(d["res"])
A0 = np.array([[-2, 1, 0, 0], [1, -2, 1, 0], [0, 1, -2, 1], [0, 0, 1, -1.0]])
ev, V = np.linalg.eig(A0); Vi = np.linalg.inv(V)
def G(f, w, row=3):  # y_row / x of the open ladder, via eigen-decomposition
    s = 2j * np.pi * np.asarray(f)[:, None]
    return ((V[row] * Vi[:, 0])[None, :] * w / (s - w * ev[None, :])).sum(1)
def resp(f, w, k, g, fh=100.0):
    # output = g (y3 + y4) / 2 (fits the ~18 dB/oct roll-off; 0.48 dB mean)
    s = 2j * np.pi * np.asarray(f); hp = s / (s + 2 * np.pi * fh); G4 = G(f, w)
    return g * 0.5 * (G(f, w, 2) + G4) / (1 + k * hp * G4)
out = {}
for ri in range(len(R)):
    rows = []
    x0 = [300.0, 0.5, 1.0]
    for c in range(0, 128, 4):
        h = H[4, ri, c]; mag = 20 * np.log10(np.abs(h) + 1e-12)
        keep = (mag > mag.max() - 70) & (F > 25) & (F < 17000)
        res = lambda p: 20 * np.log10(np.abs(resp(F[keep], *p)) + 1e-12) - mag[keep]
        best = None
        for w0 in (x0[0], x0[0] * 1.3, x0[0] * 2):
            try: r = least_squares(res, [w0, x0[1], x0[2]], bounds=([5, 0, 0.01], [8e5, 80, 100]))
            except Exception: continue
            if best is None or r.cost < best.cost: best = r
        x0 = list(best.x)
        rows.append([c] + [float(v) for v in best.x] + [float(np.sqrt(np.mean(best.fun ** 2)))])
    out[int(R[ri])] = rows
    print(f"res {R[ri]:3d}: " + " ".join(f"c{r[0]}:w{r[1]:.0f}/k{r[2]:.2f}/g{r[3]:.2f}/e{r[4]:.1f}" for r in rows[::4]))
(s1.ROOT / "reference/meta/filter_lpdl.json").write_text(json.dumps({"res": [int(r) for r in R], "cut": list(range(0, 128, 4)), "fits": out}))
