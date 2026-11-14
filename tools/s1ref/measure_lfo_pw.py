#!/usr/bin/env python3
"""LFO -> pulse width, original vs ours. Osc1 pulse at key 36 (one cycle =
674 samples), LFO1 key-synced slow triangle (speed 20, -1 at the note-on,
rising to +1 at 1.89 s) on dest 5. The duty (fraction of each cycle above
the cycle's mid level) is measured cycle by cycle in the time domain, so
d and 1 - d stay distinct (harmonic fits fold them).
Usage: measure_lfo_pw.py [pw ...]   (default pw 64). Prints duty at a few
LFO positions per depth and writes reference/meta/lfo_pw.json."""
import json
import sys

import numpy as np

import s1

SR = 44100
KEY = 36
L = 2.0
DEPTHS = [0, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127]
TRI = dict(p0=2, p57=1, p41=5, p42=1, p43=20, p68=1)


def duties(x):
    s = np.asarray(x[0], float)
    n = SR / s1.key_hz(KEY)
    out = []
    for k in range(int(len(s) / n) - 1):
        c = s[int(round(k * n)):int(round((k + 1) * n))]
        mid = 0.5 * (c.max() + c.min())
        out.append(((k + 0.5) * n / SR, float((c > mid).mean()) if c.max() - c.min() > 1e-4 else float("nan")))
    return out


def measure(pw, ours):
    outs = s1.render_many([dict(native=s1.patch(p8=pw, p44=d, **TRI), notes=((0, L, KEY, 127),), length=L)
                           for d in DEPTHS], ours=ours)
    return [duties(x) for x, _ in outs]


def main():
    pws = [int(a) for a in sys.argv[1:]] or [64]
    res = {}
    for pw in pws:
        a, b = measure(pw, False), measure(pw, True)
        res[pw] = {"s1": a, "s2": b}
        print(f"pw {pw}: duty at t = 0.05 / 0.5 / 0.95 / 1.4 / 1.85 s  (s1 | s2)")
        for d, ca, cb in zip(DEPTHS, a, b):
            pick = lambda c: [c[min(range(len(c)), key=lambda i: abs(c[i][0] - t))][1] for t in (0.05, 0.5, 0.95, 1.4, 1.85)]
            print(f"  {d:4d}  {np.round(pick(ca), 3).tolist()}  |  {np.round(pick(cb), 3).tolist()}")
    (s1.ROOT / "reference/meta/lfo_pw.json").write_text(json.dumps({"depths": DEPTHS, "pw": res}))


if __name__ == "__main__":
    main()
