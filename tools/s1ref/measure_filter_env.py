#!/usr/bin/env python3
"""Filter env -> cutoff law (static: attack / decay 0, the env sits at the
sustain level S / 127), effective cutoff from cutoff_probe (noise, LP12).

  measure_filter_env.py [--ours]

Measured on the original: the env sweeps from base - 2.1 (env 0) to the env-1
TARGET clamp(base + depth, -2, 125), base = cutoff knob + kbd track, depth =
(amount - 64) * 1.984 (x velocity scale when the switch is on), linear in
the env; the clamp is on the target, not on the swept result. Prints the
measured knob next to that law.
"""
import re
import sys
from pathlib import Path

import numpy as np

import cutoff_probe as C
import s1

L = 1.5
# (cutoff, amount, sustain, kbd track, key, vel switch, velocity)
CASES = [(c, a, s, 0, 60, 0, 127) for c in (20, 50, 81) for a in (0, 40, 64, 80, 96, 105, 127) for s in (0, 30, 60, 127)]
CASES += [(60, 96, 60, 127, 84, 0, 127), (60, 96, 60, 127, 60, 0, 127), (127, 8, 127, 0, 60, 0, 127),
          (81, 127, 60, 1, 64, 1, 64), (81, 127, 60, 1, 30, 1, 30), (30, 127, 127, 0, 60, 1, 64)]


HPP = (Path(__file__).resolve().parents[2] / "EngineCore/MeasuredTables.hpp").read_text()


def table(name):
    body = re.search(name + r"\[128\] = \{(.*?)\};", HPP, re.S).group(1)
    return [float(v) for v in re.findall(r"[-\d.e]+(?=f)", body)]


KBD, VEL = table("kKbdTrack"), table("kFilterVelScale")


def law(c, a, s, t, key, sw, vel):
    base = c + KBD[t] * (key - 48) * 12.624 / 12
    depth = (a - 64) * 1.984 * (VEL[vel] if sw else 1.0)
    low = base - 2.1
    return low + (np.clip(base + depth, -2, 125) - low) * s / 127


def main():
    specs = [dict(native=C.noise_patch(p19=c, p21=a, p15=0, p16=0, p17=s, p22=t, p24=sw), notes=((0, L, key, vel),),
                  length=L) for c, a, s, t, key, sw, vel in CASES]
    renders = s1.render_many(specs, ours="--ours" in sys.argv)
    errs = []
    print("cut amt sus trk key sw vel   measured    law")
    for case, (x, _) in zip(CASES, renders):
        k, m = C.cutoff_knob(x, 0.6, 1.4, L), law(*case)
        if -1 < m < 124: errs.append(k - m)  # probe floor / fc top excluded
        print(" ".join(f"{v:3d}" for v in case), f"  {k:7.1f} {m:7.1f}")
    print(f"error in range: mean {np.mean(errs):+.2f}, max |{np.abs(errs).max():.2f}| knob units")


if __name__ == "__main__":
    main()
