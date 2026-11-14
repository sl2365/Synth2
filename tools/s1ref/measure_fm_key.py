#!/usr/bin/env python3
"""FM depth vs key, key shift and sample rate (osc1 sine carrier, osc2
triangle a fifth up, FM 50): fitted beta of inst_freq1 = f1 (1 + beta tri2)
from the sideband amplitudes (same method as measure_fm.py).

  measure_fm_key.py [--ours]

Measured on the original: beta doubles per octave down (keys and key shift
alike) and grows with the sample rate: the deviation is a FIXED phase step
per sample, beta[v] x 261.63 Hz at 44.1 kHz (fm.json's table is at key 60).
"""
import sys

import numpy as np
from scipy.optimize import minimize_scalar

import s1

KS = range(-6, 7)
tri = lambda ph: 1 - 4 * np.abs((ph % 1.0) - 0.5)


def lines(sig, t, sr, f1, f2):
    fr = sorted({(k, abs(f1 + k * f2)) for k in KS if 0 < abs(f1 + k * f2) < sr / 2}, key=lambda q: q[1])
    cols = [np.ones_like(t)]
    for _, f in fr:
        cols += [np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)]
    c, *_ = np.linalg.lstsq(np.stack(cols, 1), sig, rcond=None)
    d = {}
    for i, (k, _) in enumerate(fr):
        d[k] = d.get(k, 0) + np.hypot(c[1 + 2 * i], c[2 + 2 * i])
    return np.array([d.get(k, 0) for k in KS])


def beta(x, sr, f1):
    f2 = f1 * 2 ** (7 / 12)
    n = int(.5 * sr)
    t = np.arange(n) / sr
    sig = np.asarray(x[0][int(.3 * sr):int(.3 * sr) + n], float)
    target = lines(sig, t, sr, f1, f2)
    model = lambda b: lines(0.2 * np.sin(2 * np.pi * np.cumsum(f1 * (1 + b * tri(f2 * t))) / sr), t, sr, f1, f2)
    err = lambda b: np.sum((model(b) - target) ** 2)
    b0 = min(np.linspace(0, 4, 81), key=err)
    return minimize_scalar(err, bounds=(max(0, b0 - .05), b0 + .05), method="bounded").x


def main():
    ours = "--ours" in sys.argv
    cases = [(48, 0, 44100), (60, 0, 44100), (72, 0, 44100), (60, -12, 44100), (60, 12, 44100), (60, 0, 48000),
             (60, 0, 96000)]
    print("key  ks     sr | beta   law (1.196 at key 60, 44.1 kHz)")
    for key, ks, sr in cases:
        (x, _), = s1.render_many([dict(native=s1.patch(p0=0, p1=3, p2=71, p45=50, p91=1, p9=ks),
                                       notes=((0, 1.0, key, 127),), length=1.0, sr=sr)], ours=ours)
        f1 = s1.key_hz(key + ks)
        print(f"{key:3d} {ks:+3d} {sr:6d} | {beta(x, sr, f1):.3f}  {1.196 * s1.key_hz(60) / f1 * sr / 44100:.3f}")


if __name__ == "__main__":
    main()
