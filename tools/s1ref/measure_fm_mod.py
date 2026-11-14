#!/usr/bin/env python3
"""Mod env -> FM and LFO -> FM, original vs ours. Same probe as measure_fm.py
(osc1 sine carrier at key 60, osc2 triangle +7 semis): the FM index beta is
fitted in a window and expressed as the equivalent FM KNOB (inverse of the
measured beta table, fractional). Mod env: attack 0, decay 127 (env ~1 in
the window). LFO1: key-synced slow square (speed 20, +1 for 1.89 s, then -1).
Writes reference/meta/fm_mod.json."""
import json

import numpy as np
from scipy.optimize import minimize_scalar

import s1
from measure_fm import KS, F1, F2, SR, N, lines, model

BETA = np.array(json.load(open(s1.ROOT / "reference/meta/fm.json"))["beta"])
GRID = np.arange(0, 127.01, 0.25)


def beta_of(knob):
    return float(np.interp(knob, np.arange(128), BETA))


def knob_fit(sig):
    ref = lines(sig)
    cost = lambda k: float(np.sum((lines(model(beta_of(k))) - ref) ** 2))
    k0 = GRID[int(np.argmin([cost(k) for k in GRID[::4]])) * 4]
    r = minimize_scalar(cost, bounds=(max(0, k0 - 1.5), min(127, k0 + 1.5)), method="bounded", options={"xatol": 1e-3})
    return float(r.x)


def win(x, a):
    return np.asarray(x[0][int(a * SR):int(a * SR) + N], float)


BASES = [0, 32, 64]
AMTS = [0, 16, 32, 48, 56, 64, 72, 80, 96, 112, 127]
DEPTHS = [0, 8, 16, 32, 48, 64, 96, 127]
CARRIER = dict(p0=0, p1=3, p2=71)


def measure(ours):
    out = {}
    specs = [dict(native=s1.patch(p45=b, p10=1, p71=1, p11=a, p12=0, p13=127, **CARRIER), notes=((0, 0.7, 60, 127),),
                  length=0.7) for b in BASES for a in AMTS]
    outs = s1.render_many(specs, ours=ours)
    out["modenv"] = np.array([knob_fit(win(x, 0.1)) for x, _ in outs]).reshape(len(BASES), len(AMTS)).tolist()
    sq = dict(p57=1, p41=6, p42=2, p43=20, p68=1)
    specs = [dict(native=s1.patch(p45=b, p44=d, **sq, **CARRIER), notes=((0, 2.9, 60, 127),), length=2.9)
             for b in BASES for d in DEPTHS]
    outs = s1.render_many(specs, ours=ours)
    out["lfo"] = np.array([[knob_fit(win(x, 0.3)), knob_fit(win(x, 2.2))] for x, _ in outs]).reshape(
        len(BASES), len(DEPTHS), 2).tolist()
    return out


def main():
    a, b = measure(False), measure(True)
    (s1.ROOT / "reference/meta/fm_mod.json").write_text(
        json.dumps({"bases": BASES, "amts": AMTS, "depths": DEPTHS, "s1": a, "s2": b}))
    for i, base in enumerate(BASES):
        print(f"FM knob {base}: mod env amt -> equivalent knob (s1 | s2)")
        print("  " + "  ".join(f"{am}:{a['modenv'][i][j]:.1f}|{b['modenv'][i][j]:.1f}" for j, am in enumerate(AMTS)))
        print(f"FM knob {base}: LFO depth -> knob at lfo +1 / -1 (s1 | s2)")
        print("  " + "  ".join(f"{d}:{a['lfo'][i][j][0]:.1f}/{a['lfo'][i][j][1]:.1f}|{b['lfo'][i][j][0]:.1f}/{b['lfo'][i][j][1]:.1f}"
                               for j, d in enumerate(DEPTHS)))


if __name__ == "__main__":
    main()
