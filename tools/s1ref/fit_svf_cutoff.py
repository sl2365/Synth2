#!/usr/bin/env python3
"""SVF frequency coefficient f per cutoff knob (types 0/2/3, res 64 with
d = 1 - r, g = 1 - 0.7 r fixed by fit_svf exploration). Writes
reference/meta/filter_svf_cutoff.json."""
import json
import numpy as np
from scipy.optimize import least_squares
import fit_svf as S
import s1


def main():
    d = np.load(s1.ROOT / "reference/derived/filter_tf.npz")
    H, F, R = d["H"], d["freqs"], list(d["res"])
    out = {}
    for t, kind in ((0, "lp"), (2, "hp"), (3, "bp")):
        fs, errs = [], []
        x0 = 0.0005
        for c in range(128):
            res_vals = [32, 64, 96]
            def resid(p):
                rr = []
                for r in res_vals:
                    h = H[t, R.index(r), c]
                    mag = 20 * np.log10(np.abs(h) + 1e-12)
                    keep = (mag > mag.max() - 40) & (F > 150) & (F < 19500)
                    rn = r / 127
                    m = (1 - 0.7 * rn) * S.svf2(F[keep], p[0], 1 - rn, kind)
                    rr.append(20 * np.log10(np.abs(m) + 1e-12) - mag[keep])
                    rr.append(10 * np.angle(m / h[keep]))
                return np.nan_to_num(np.concatenate(rr), nan=1e3, posinf=1e3, neginf=-1e3)
            best = None
            for start in np.clip([x0, x0 * 1.1, x0 * 0.9], 1e-5, 2.0):
                r = least_squares(resid, [start], bounds=([1e-6], [2.1]), diff_step=1e-4)
                if best is None or r.cost < best.cost:
                    best = r
            e = resid(best.x)
            fs.append(float(best.x[0]))
            errs.append(float(np.sqrt(np.mean(e[: len(e) // 2] ** 2))))
            x0 = best.x[0] * 1.06
        out[t] = {"f": fs, "rms_err": errs}
        print("type", t, "f:", [round(v, 5) for v in fs[::8]], fs[-1], "worst rms err", round(max(errs), 2), flush=True)
    (s1.ROOT / "reference/meta/filter_svf_cutoff.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
