#!/usr/bin/env python3
"""LFO -> cutoff depth, original vs ours. Slow key-synced square LFO
(starts +1):
window A = LFO +1, window B = LFO -1. Each window's effective cutoff
(cutoff_probe) is mapped to filter-freq knob units through a calibration
made with the SAME timing on the SAME engine (static knob values), so the
probe's bias cancels. Writes reference/meta/lfo_cutoff_<base>.json."""
import json
import sys

import numpy as np

import cutoff_probe as C
import s1

# Key-synced square at speed 20 (0.264 Hz): + from 0 to 1.89 s, - to 3.79 s.
L = 3.8
A, B = (0.25, 1.7), (2.15, 3.6)
DEPTHS = [0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127]
CAL = list(range(128))


def spec(**kw):
    return dict(native=C.noise_patch(**kw), notes=((0, L, 60, 127),), length=L)


def probe(outs, w):
    return [C.cutoff_knob(x, *w, L) for x, _ in outs]


def to_knob(cal_probe, values):
    c = np.array(cal_probe)
    ok = np.ones(len(c), bool)
    best = -np.inf
    for i in range(len(c)):  # keep the monotonic part (closed-filter fits fail)
        ok[i] = c[i] > best and not (CAL[i] < 40 and c[i] > 100)
        if ok[i]:
            best = c[i]
    xs, ks = c[ok], np.array(CAL)[ok]
    return [float(np.interp(v, xs, ks)) for v in values]


def main():
    base = int(sys.argv[1]) if len(sys.argv) > 1 else 64
    res = {}
    for who, ours in (("s1", False), ("s2", True)):
        cal = probe(s1.render_many([spec(p19=k) for k in CAL], ours=ours), A)
        outs = s1.render_many([spec(p19=base, p57=1, p42=2, p43=20, p68=1, p41=3, p44=d) for d in DEPTHS], ours=ours)
        hi, lo = to_knob(cal, probe(outs, A)), to_knob(cal, probe(outs, B))
        res[who] = [[h - base, l - base] for h, l in zip(hi, lo)]
    path = s1.ROOT / f"reference/meta/lfo_cutoff_{base}.json"
    path.write_text(json.dumps(dict(base=base, depths=DEPTHS, **res), indent=1))
    print(f"base {base}   depth: s1 (+, -)   s2 (+, -)")
    for i, d in enumerate(DEPTHS):
        print(f"{d:4d}  s1 {res['s1'][i][0]:7.2f} {res['s1'][i][1]:7.2f}   s2 {res['s2'][i][0]:7.2f} {res['s2'][i][1]:7.2f}")


if __name__ == "__main__":
    main()
