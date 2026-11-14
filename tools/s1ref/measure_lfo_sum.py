#!/usr/bin/env python3
"""Both LFOs on the SAME destination: how does the original combine them?
Key-synced squares: LFO1 speed 20 (+ 0..1.89 s, - to 3.79 s), LFO2 speed 8
(+ 0..3.76 s), so window A = (+1, +1) and window B = (-1, +1). Each
(depth1, depth2) pair is compared with the single-LFO renders: sum of the
effects vs one LFO at the summed depth. Prints s1 / s2 side by side and
writes reference/meta/lfo_sum.json."""
import json

import numpy as np

import instfreq
import s1

SR = 44100
L = 3.6
A, B = (0.25, 1.7), (2.15, 3.5)
PAIRS = [(0, 0), (32, 0), (0, 32), (32, 32), (16, 48), (48, 16), (64, 64), (0, 64), (64, 0), (96, 96), (127, 127), (0, 127), (127, 0)]


def lfos(dest, d1, d2):
    return dict(p57=1, p42=2, p43=20, p68=1, p41=dest, p44=d1, p58=1, p47=2, p48=8, p70=1, p46=dest, p49=d2)


def win(x, w, ch=0):
    return np.asarray(x[ch][int(w[0] * SR):int(w[1] * SR)], float)


def rms(x, w, ch=0):
    return float(np.sqrt((win(x, w, ch) ** 2).mean()))


def measure(ours):
    out = {}
    # pitch, osc1 + osc2 (dest 2): osc1 sine alone, key 60
    ref = s1.key_hz(60)
    for dest, name in ((2, "pitch12_cents"), (1, "pitch2_cents")):
        osc = dict(p0=0, p5=0) if dest == 2 else dict(p0=0, p1=3, p5=127)  # osc2 tri alone for dest 1
        outs = s1.render_many([dict(native=s1.patch(**osc, **lfos(dest, a, b)), notes=((0, L, 60, 127),), length=L)
                               for a, b in PAIRS], ours=ours)
        out[name] = [[float(np.median(instfreq.inst_cents(x, SR, ref)[int(w[0] * SR):int(w[1] * SR)])) for w in (A, B)]
                     for x, _ in outs]
    # amp (dest 4): sine at key 84, rms relative to no LFO
    outs = s1.render_many([dict(native=s1.patch(p0=0, **lfos(4, a, b)), notes=((0, L, 84, 127),), length=L)
                           for a, b in PAIRS], ours=ours)
    r0 = rms(outs[0][0], A)
    out["amp"] = [[rms(x, A) / r0, rms(x, B) / r0] for x, _ in outs]
    # pan (dest 7): L / R per window
    outs = s1.render_many([dict(native=s1.patch(p0=0, **lfos(7, a, b)), notes=((0, L, 84, 127),), length=L)
                           for a, b in PAIRS], ours=ours)
    r0 = rms(outs[0][0], A)
    out["pan_LR"] = [[rms(x, w, c) / r0 for w in (A, B) for c in (0, 1)] for x, _ in outs]
    return out


# Part 2, every sign pair: LFO1 speed 20 (+ to 1.89 s), LFO2 speed 32 (+ to
# 0.95 s, - to 1.89, + to 2.84, - to 3.79): windows (+,+) (+,-) (-,+) (-,-).
W4 = [(0.25, 0.9), (1.05, 1.8), (1.98, 2.75), (2.93, 3.7)]
L4 = 3.8
PAIRS4 = [(32, 0), (0, 32), (64, 0), (0, 64), (32, 32), (16, 48), (64, 64), (96, 96), (127, 127)]


def lfos4(dest, d1, d2):
    return dict(p57=1, p42=2, p43=20, p68=1, p41=dest, p44=d1, p58=1, p47=2, p48=32, p70=1, p46=dest, p49=d2)


def measure4(ours):
    import cutoff_probe as C
    from measure_fm_mod import knob_fit, CARRIER
    out = {}
    outs = s1.render_many([dict(native=s1.patch(p0=0, **lfos4(4, a, b)), notes=((0, L4, 84, 127),), length=L4)
                           for a, b in [(0, 0)] + PAIRS4], ours=ours)
    r0 = rms(outs[0][0], W4[0])
    out["amp"] = [[rms(x, w) / r0 for w in W4] for x, _ in outs[1:]]
    F = s1.key_hz(48)
    outs = s1.render_many([dict(native=s1.patch(p0=2, p8=64, **lfos4(5, a, b)), notes=((0, L4, 48, 127),), length=L4)
                           for a, b in PAIRS4], ours=ours)
    ds = np.linspace(0.0005, 0.9995, 1999)
    out["pw_duty"] = []
    for x, _ in outs:
        row = []
        for w in W4:
            hs, a, _ = s1.harmonics(x, SR, F, 10, start=w[0], dur=w[1] - w[0])
            model = np.abs(np.sin(np.pi * np.outer(ds, hs))) / hs
            model /= model[:, :1] + 1e-12
            row.append(float(ds[np.argmin(((model - a / a[0]) ** 2).sum(1))]))
        out["pw_duty"].append(row)
    # cutoff (dest 3), base 40: probe calibrated on static knobs, same timing / engine
    cal_k = list(range(0, 128, 2))
    cal = [C.cutoff_knob(x, *W4[0], L4) for x, _ in s1.render_many(
        [dict(native=C.noise_patch(p19=k), notes=((0, L4, 60, 127),), length=L4) for k in cal_k], ours=ours)]
    outs = s1.render_many([dict(native=C.noise_patch(p19=40, **lfos4(3, a, b)), notes=((0, L4, 60, 127),), length=L4)
                           for a, b in PAIRS4], ours=ours)
    out["cutoff_knob"] = [[float(np.interp(C.cutoff_knob(x, *w, L4), cal, cal_k)) for w in W4] for x, _ in outs]
    # FM (dest 6), base 32: equivalent FM knob per window
    outs = s1.render_many([dict(native=s1.patch(p45=32, **CARRIER, **lfos4(6, a, b)), notes=((0, L4, 60, 127),),
                                length=L4) for a, b in PAIRS4], ours=ours)
    out["fm_knob"] = [[knob_fit(np.asarray(x[0][int(w[0] * SR):int(w[0] * SR) + int(0.5 * SR)], float)) for w in W4]
                      for x, _ in outs]
    return out


def main():
    res = {"s1": measure(False), "s2": measure(True), "pairs": PAIRS}
    res4 = {"s1": measure4(False), "s2": measure4(True), "pairs": PAIRS4, "windows": "(+,+) (+,-) (-,+) (-,-)"}
    (s1.ROOT / "reference/meta/lfo_sum.json").write_text(json.dumps(dict(two=res, four=res4), indent=1))
    for key in ("pitch12_cents", "pitch2_cents", "amp", "pan_LR"):
        print(key, "(windows A = (+,+), B = (-,+))")
        for i, (a, b) in enumerate(PAIRS):
            print(f"  {a:3d} {b:3d}  s1 {np.round(res['s1'][key][i], 3).tolist()}   s2 {np.round(res['s2'][key][i], 3).tolist()}")
    for key in ("amp", "pw_duty", "cutoff_knob", "fm_knob"):
        print(key, "(windows (+,+) (+,-) (-,+) (-,-))")
        for i, (a, b) in enumerate(PAIRS4):
            print(f"  {a:3d} {b:3d}  s1 {np.round(res4['s1'][key][i], 3).tolist()}   s2 {np.round(res4['s2'][key][i], 3).tolist()}")


if __name__ == "__main__":
    main()
