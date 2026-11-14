#!/usr/bin/env python3
"""Measure an envelope's laws over all 128 knob values.

Amp env: sine carrier, demodulated. Filter env: not here (see filter tools).
Writes reference/meta/amp_env.json:
  attack_s[v]  linear ramp time to full scale
  decay_tau[v] exponential time constant (to sustain 0)
  release_tau[v]
  sustain[v]   level relative to full scale
"""
import json

import numpy as np

import envelope as E
import s1

K = 96
F = s1.key_hz(K)
SR = 44100
VALS = list(range(128))


def fit_tau(env, start, lo=0.05, hi=0.8):
    """Exponential time constant of env (normalized to 1 at `start`) between
    levels hi..lo; returns seconds."""
    seg = env[start:]
    i0 = np.argmax(seg <= hi)
    i1 = np.argmax(seg <= lo)
    if i1 <= i0 + 3:
        return None
    t = np.arange(i0, i1) / SR
    y = np.log(np.maximum(seg[i0:i1], 1e-9))
    slope = np.polyfit(t, y, 1)[0]
    return -1.0 / slope


def main():
    top = None
    out = {}

    def run(specs):
        return s1.render_many(specs)

    # full-scale reference: attack 0, sustain 127
    (x, _), = run([dict(native=s1.patch(p0=0, p25=0, p26=0, p27=127, p28=0), notes=((0, 1, K, 127),), length=0.6)])
    top = float(np.median(E.demod(x, SR, F, 2)[int(0.2 * SR) : int(0.5 * SR)]))
    out["full_scale"] = top

    att_len = lambda v: min(40.0, 0.05 + 1.3 * 0.0005 * 2 ** (v / 8.05))
    outs = run([dict(native=s1.patch(p0=0, p25=v, p26=0, p27=127, p28=0), notes=((0, att_len(v) + 1, K, 127),),
                     length=att_len(v) + 0.1) for v in VALS])
    attack = []
    for v, (x, _) in zip(VALS, outs):
        env = E.demod(x, SR, F, 1.0) / top
        t90 = E.crossing(env, 0.9)
        attack.append(t90 / 0.9 / SR if t90 else None)
    out["attack_s"] = attack

    # decay to sustain 0 from full scale; length adapts to the expected tau
    dec_len = lambda v: min(60.0, 0.1 + 12 * 0.03 * 2 ** ((v - 48) / 10.3))
    outs = run([dict(native=s1.patch(p0=0, p25=0, p26=v, p27=0, p28=0), notes=((0, dec_len(v), K, 127),),
                     length=dec_len(v)) for v in VALS])
    out["decay_tau"] = [fit_tau(E.demod(x, SR, F, 1.0) / top, int(0.0015 * SR)) for x, _ in outs]

    # release from full sustain, note off at 0.1 s
    outs = run([dict(native=s1.patch(p0=0, p25=0, p26=0, p27=127, p28=v), notes=((0, 0.1, K, 127),),
                     length=0.1 + dec_len(v)) for v in VALS])
    out["release_tau"] = [fit_tau(E.demod(x, SR, F, 1.0) / top, int(0.1 * SR)) for x, _ in outs]

    outs = run([dict(native=s1.patch(p0=0, p25=0, p26=0, p27=v, p28=0), notes=((0, 1, K, 127),), length=0.6)
                for v in VALS])
    out["sustain"] = [float(np.median(E.demod(x, SR, F, 2)[int(0.2 * SR) : int(0.5 * SR)]) / top) for x, _ in outs]

    path = s1.ROOT / "reference/meta/amp_env.json"
    path.write_text(json.dumps(out, indent=1))
    print("wrote", path)


if __name__ == "__main__":
    main()
