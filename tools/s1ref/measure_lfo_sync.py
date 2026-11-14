#!/usr/bin/env python3
"""LFO tempo-sync divisions, original vs ours. LFO1 key-synced square ->
amp at depth 127 gates a sine on / off; the gate period in beats is
measured for every knob value. Slow steps are rendered at a high host
tempo (checked against a known step at the same tempo) to stay short.
Usage: measure_lfo_sync.py [probe]   (probe = tempo range check only).
Writes reference/meta/lfo_sync.json (beats per LFO cycle, knob 0..127)."""
import json
import sys

import numpy as np

import s1

SR = 44100
KEY = 96
GATE = dict(p0=0, p57=1, p41=4, p42=2, p44=127, p67=1, p68=1)


def period(x, w=int(0.0005 * SR)):
    a = np.abs(np.asarray(x[0], float))
    env = np.maximum.reduce([np.roll(a, i) for i in range(w)])
    on = env > 0.5 * env.max()
    rise = np.flatnonzero(on[1:] & ~on[:-1]) + 1
    edges = np.flatnonzero(on[1:] != on[:-1]) + 1
    if len(rise) >= 2:
        return (rise[-1] - rise[0]) / (len(rise) - 1) / SR
    return 2 * float(np.median(np.diff(edges))) / SR if len(edges) >= 2 else float("nan")


def spec(v, tempo, beats):
    length = max(0.5, 2.6 * beats * 60 / tempo)
    return dict(native=s1.patch(p43=v, **GATE), notes=((0, length, KEY, 127),), length=length, tempo=tempo)


def run(jobs, ours):
    outs = s1.render_many([spec(*j) for j in jobs], ours=ours)
    return [period(x) * t / 60 for (x, _), (_, t, _) in zip(outs, jobs)]


def main():
    if sys.argv[1:] == ["probe"]:
        jobs = [(77, t, 1.0) for t in (120, 240, 480, 960, 1920)] + [(39, t, 8.0) for t in (120, 480, 960)]
        for j, b in zip(jobs, run(jobs, False)):
            print(f"  knob {j[0]:3d} tempo {j[1]:5d}  beats {b:.4f}")
        return
    # expected beats only size the render; tempo keeps slow cycles short
    guess = [64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3.5, 3, 2, 1.75, 1.5, 4 / 3, 1, 0.875, 0.75, 2 / 3, 0.5,
             0.375, 1 / 3, 0.25, 1 / 6, 0.125, 1 / 12, 1 / 24]
    jobs = []
    for v in range(128):
        b = guess[v * 30 // 128]
        jobs.append((v, int(TEMPO if b >= 4 else 120), b))
    res = {"s1": run(jobs, False), "s2": run(jobs, True), "tempo": [j[1] for j in jobs]}
    (s1.ROOT / "reference/meta/lfo_sync.json").write_text(json.dumps(res, indent=1))
    for v in range(128):
        a, b = res["s1"][v], res["s2"][v]
        flag = "" if abs(a - b) < 0.01 * a else "  <-- differs"
        print(f"  {v:3d}  s1 {a:8.4f}  s2 {b:8.4f}{flag}")


TEMPO = 960

if __name__ == "__main__":
    main()
