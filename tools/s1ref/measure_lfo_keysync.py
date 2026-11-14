#!/usr/bin/env python3
"""LFO depth laws per destination, original vs ours, with a KEY-SYNCED slow
square LFO (speed 20 = 0.264 Hz, + from 0 to 1.89 s, - to 3.79 s), so
window A is cleanly LFO = +1 and window B LFO = -1 (the free-running LFO of
measure_lfo.py straddled both windows). Prints s1 / s2 side by side and
writes reference/meta/lfo_keysync.json."""
import json

import numpy as np

import instfreq
import s1

SR = 44100
L = 3.8
A, B = (0.25, 1.7), (2.15, 3.6)
DEPTHS = [0, 8, 16, 32, 48, 64, 80, 96, 112, 127]
SQ = dict(p57=1, p42=2, p43=20, p68=1)


def win(x, w, ch=0):
    return np.asarray(x[ch][int(w[0] * SR):int(w[1] * SR)], float)


def rms(x, w, ch=0):
    return float(np.sqrt((win(x, w, ch) ** 2).mean()))


def measure(ours):
    out = {}
    # pitch, osc1 + osc2 (dest 2): osc1 sine alone, key 60
    outs = s1.render_many([dict(native=s1.patch(p0=0, p5=0, p41=2, p44=d, **SQ), notes=((0, L, 60, 127),), length=L)
                           for d in DEPTHS], ours=ours)
    ref = s1.key_hz(60)
    out["pitch_cents"] = []
    for x, _ in outs:
        c = instfreq.inst_cents(x, SR, ref)
        out["pitch_cents"].append([float(np.median(c[int(w[0] * SR):int(w[1] * SR)])) for w in (A, B)])
    # amp (dest 4): sine at key 84, rms relative to depth 0
    outs = s1.render_many([dict(native=s1.patch(p0=0, p41=4, p44=d, **SQ), notes=((0, L, 84, 127),), length=L)
                           for d in DEPTHS], ours=ours)
    r0 = rms(outs[0][0], A)
    out["amp"] = [[rms(x, A) / r0, rms(x, B) / r0] for x, _ in outs]
    # pan (dest 7): L / R rms in each half, relative to depth 0
    outs = s1.render_many([dict(native=s1.patch(p0=0, p41=7, p44=d, **SQ), notes=((0, L, 84, 127),), length=L)
                           for d in DEPTHS], ours=ours)
    r0 = rms(outs[0][0], A)
    out["pan_LR"] = [[rms(x, w, c) / r0 for w in (A, B) for c in (0, 1)] for x, _ in outs]
    # pitch near full depth (where the old estimator broke down)
    hi = list(range(112, 128))
    outs = s1.render_many([dict(native=s1.patch(p0=0, p5=0, p41=2, p44=d, **SQ), notes=((0, L, 60, 127),), length=L)
                           for d in hi], ours=ours)
    out["pitch_hi_depths"] = hi
    out["pitch_hi_cents"] = [float(np.median(instfreq.inst_cents(x, SR, ref)[int(A[0] * SR):int(A[1] * SR)]))
                             for x, _ in outs]
    # pulse width (dest 5): osc1 pulse at pw 64 (square), duty per half via a harmonic fit
    F = s1.key_hz(48)
    outs = s1.render_many([dict(native=s1.patch(p0=2, p8=64, p41=5, p44=d, **SQ), notes=((0, L, 48, 127),), length=L)
                           for d in DEPTHS], ours=ours)
    ds = np.linspace(0.0005, 0.9995, 1999)
    out["pw_duty"] = []
    for x, _ in outs:
        row = []
        for w in (A, B):
            hs, a, _ = s1.harmonics(x, SR, F, 10, start=w[0], dur=w[1] - w[0])
            model = np.abs(np.sin(np.pi * np.outer(ds, hs))) / hs
            model /= model[:, :1] + 1e-12
            row.append(float(ds[np.argmin(((model - a / a[0]) ** 2).sum(1))]))
        out["pw_duty"].append(row)
    return out


def main():
    res = {"s1": measure(False), "s2": measure(True), "depths": DEPTHS}
    (s1.ROOT / "reference/meta/lfo_keysync.json").write_text(json.dumps(res, indent=1))
    print("pitch hi", list(zip(res["s1"]["pitch_hi_depths"], np.round(res["s1"]["pitch_hi_cents"], 1).tolist())))
    for key in ("pitch_cents", "amp", "pan_LR", "pw_duty"):
        print(key)
        for i, d in enumerate(DEPTHS):
            print(f"  {d:4d}  s1 {np.round(res['s1'][key][i], 3).tolist()}   s2 {np.round(res['s2'][key][i], 3).tolist()}")


if __name__ == "__main__":
    main()
