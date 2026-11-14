#!/usr/bin/env python3
"""SUPERSEDED for cutoff / amp / pan / pitch near full depth: the free-running
LFO straddled both windows (see measure_lfo_keysync.py, measure_lfo_cutoff.py).
LFO depth laws per destination with a slow square LFO (starts at +1):
window A = + half, window B = - half. Appends to reference/meta/lfo_depth.json."""
import json
import numpy as np
import cutoff_probe as C
import s1

SR = 44100
L = 2.2
A, B = (0.25, 0.95), (1.3, 2.0)
DEPTHS = list(range(0, 128, 4)) + [127]


def win(x, w, ch=0):
    return np.asarray(x[ch][int(w[0] * SR):int(w[1] * SR)], float)


def main():
    path = s1.ROOT / "reference/meta/lfo_depth.json"
    out = json.loads(path.read_text())
    sq = dict(p57=1, p42=2, p43=30)
    # cutoff (knob units): noise through LP12 at 60
    outs = s1.render_many([dict(native=C.noise_patch(p19=60, p41=3, p44=d, **sq), notes=((0, L, 60, 127),), length=L)
                           for d in DEPTHS])
    out["cutoff_knob"] = [(C.cutoff_knob(x, *A, L) - C.cutoff_knob(x, *B, L)) / 2 for x, _ in outs]
    # amp: sine osc, level ratio
    outs = s1.render_many([dict(native=s1.patch(p0=0, p41=4, p44=d, **sq), notes=((0, L, 84, 127),), length=L)
                           for d in DEPTHS])
    out["amp_hi_lo"] = [[float(np.sqrt((win(x, A) ** 2).mean()) / 0.1415), float(np.sqrt((win(x, B) ** 2).mean()) / 0.1415)]
                        for x, _ in outs]
    # pan: L and R rms in each half
    outs = s1.render_many([dict(native=s1.patch(p0=0, p41=7, p44=d, **sq), notes=((0, L, 84, 127),), length=L)
                           for d in DEPTHS])
    out["pan_LR"] = [[float(np.sqrt((win(x, w, c) ** 2).mean()) / 0.1415) for w in (A, B) for c in (0, 1)] for x, _ in outs]
    # pulse width: osc1 pulse pw 64, duty in each half via harmonic fit
    F = s1.key_hz(48)
    outs = s1.render_many([dict(native=s1.patch(p0=2, p8=64, p41=5, p44=d, **sq), notes=((0, L, 48, 127),), length=L)
                           for d in DEPTHS])
    duty = []
    ds = np.linspace(0.0005, 0.9995, 1999)
    for x, _ in outs:
        row = []
        for w in (A, B):
            hs, a, _ = s1.harmonics(x, SR, F, 10, start=w[0], dur=w[1] - w[0])
            model = np.abs(np.sin(np.pi * np.outer(ds, hs))) / hs
            model /= model[:, :1] + 1e-12
            row.append(float(ds[np.argmin(((model - a / a[0]) ** 2).sum(1))]))
        duty.append(row)
    out["pw_duty"] = duty
    out["depths"] = DEPTHS
    path.write_text(json.dumps(out, indent=1))
    for k in ("cutoff_knob", "amp_hi_lo", "pan_LR", "pw_duty"):
        print(k, [(DEPTHS[i], np.round(out[k][i], 3).tolist()) for i in range(0, len(DEPTHS), 4)])


if __name__ == "__main__":
    main()
