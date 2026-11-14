#!/usr/bin/env python3
"""Delay tone (98) of the original: every knob value.

  measure_delay_tone.py   -> reference/meta/delay_tone.json

Echo only (time 1, feedback 0, dry / wet 127), identical noise, complex H
against tone 64 (= no filter). Each knob fits a bilinear one-pole (analog
cutoff, no prewarp): below 64 a lowpass, above a highpass (< 0.05 dB). The
laws are close to the EQ tone's (kEqToneHz) but not equal.
"""
import json

import numpy as np
from scipy.optimize import least_squares

import s1
from measure_eq import LEN, FREQS, noise_patch, tf

SR = 44100
Z1 = np.exp(-1j * 2 * np.pi * FREQS / SR)
BASE = dict(p65=1, p35=1, p36=0, p37=127, p82=0, p83=64)


def model(hz, hp):
    k = np.pi * hz / SR
    return ((1 - Z1) if hp else k * (1 + Z1)) / ((1 + k) - (1 - k) * Z1)


def main():
    knobs = [t for t in range(128) if t != 64]
    specs = [dict(native=noise_patch(**BASE, p98=t), notes=((0, LEN, 60, 127),), length=LEN) for t in [64] + knobs]
    outs = s1.render_many(specs)
    x = outs[0][0][0].astype(float)
    m = FREQS < 17500
    hz = {64: 0.0}
    for t, (y, _) in zip(knobs, outs[1:]):
        h = tf(x, y[0].astype(float))
        r = least_squares(lambda q: np.concatenate([(model(np.exp(q[0]), t > 64) - h)[m].real,
                                                    (model(np.exp(q[0]), t > 64) - h)[m].imag]), [np.log(1000.0)])
        hz[t] = float(np.exp(r.x[0]))
        err = np.abs(20 * np.log10(np.abs(model(hz[t], t > 64)[m]) / np.abs(h[m]))).max()
        print(f"tone {t:3d}: {'LP' if t < 64 else 'HP'} {hz[t]:9.1f} Hz  (max err {err:.3f} dB)")
    (s1.ROOT / "reference/meta/delay_tone.json").write_text(json.dumps({"hz": [hz[t] for t in range(128)]}, indent=1))


if __name__ == "__main__":
    main()
