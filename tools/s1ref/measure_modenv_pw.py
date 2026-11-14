#!/usr/bin/env python3
"""Mod env -> pulse width, original vs ours. Osc1 pulse at key 36, mod env
on (dest 2 = p/w), attack 0, decay 127 (env ~1 over the first cycles), the
duty (fraction above the mid level, time domain) of the first cycles after
the attack vs amount, at several pw knobs. Writes reference/meta/modenv_pw.json."""
import json

import numpy as np

import s1
from measure_lfo_pw import KEY, duties

AMOUNTS = [0, 8, 16, 24, 32, 40, 48, 56, 63, 64, 65, 72, 80, 88, 96, 104, 112, 120, 127]
PWS = [20, 64, 127]
T = 0.06  # s: duty sampled at this time (env still ~1 with decay 127)


def measure(ours):
    specs = [dict(native=s1.patch(p0=2, p8=pw, p10=1, p11=a, p12=0, p13=127, p71=2), notes=((0, 0.3, KEY, 127),),
                  length=0.3) for pw in PWS for a in AMOUNTS]
    outs = s1.render_many(specs, ours=ours)
    res = []
    for x, _ in outs:
        c = duties(x)
        res.append(min(c, key=lambda e: abs(e[0] - T))[1])
    return np.array(res).reshape(len(PWS), len(AMOUNTS)).tolist()


def main():
    a, b = measure(False), measure(True)
    (s1.ROOT / "reference/meta/modenv_pw.json").write_text(json.dumps({"amounts": AMOUNTS, "pws": PWS, "s1": a, "s2": b}))
    for i, pw in enumerate(PWS):
        print(f"pw {pw}")
        for j, am in enumerate(AMOUNTS):
            print(f"  amt {am:4d}  s1 {a[i][j]:.3f}  s2 {b[i][j]:.3f}")


if __name__ == "__main__":
    main()
