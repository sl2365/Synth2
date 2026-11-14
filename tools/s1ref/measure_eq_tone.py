#!/usr/bin/env python3
"""EQ tone (60) of the original: every knob value.

  measure_eq_tone.py   -> reference/meta/eq_tone.json

The "fixed output lowpass" (bilinear one-pole, analog 22 kHz, no prewarp)
IS the tone stage at 64: below 64 it is that lowpass at a lower cutoff
(replacing the 22 kHz one), above 64 a bilinear one-pole HIGHPASS (no
lowpass left). Against the tone-64 reference (identical noise, complex H)
each knob fits exactly (< 0.05 dB):
    LP: (1 + a) / (1 + c) * (1 + c z^-1) / (1 + a z^-1)
    HP: (1 + p) / (1 + c) * (1 - z^-1)(1 + c z^-1) / ((1 - p z^-1)(1 + z^-1))
c = the 22 kHz pole (-0.2209 at 44.1 kHz: its pole divided out); the
analog cutoff 2 fs (1 - pole) / (1 + pole) is the same at 44.1 / 48 / 96 kHz.
Writes the cutoff (Hz) per knob (64 = 22000).
"""
import json

import numpy as np
from scipy.optimize import least_squares

import s1
from measure_eq import LEN, FREQS, noise_patch, tf

SR = 44100
Z1 = np.exp(-1j * 2 * np.pi * FREQS / SR)
K22 = np.pi * 22000 / SR
C = -(1 - K22) / (1 + K22)  # 1 + C z^-1 = the 22 kHz lowpass' denominator / (1 + K)


def fit(h, t):
    m = FREQS < 17500
    if t < 64:
        model = lambda q: (1 + q[0]) / (1 + C) * (1 + C * Z1) / (1 + q[0] * Z1)
        x0, pole = [-0.5], lambda q: -q[0]
    else:
        model = lambda q: (1 + q[0]) / (1 + C) * (1 - Z1) * (1 + C * Z1) / ((1 - q[0] * Z1) * (1 + Z1))
        x0, pole = [0.9], lambda q: q[0]
    r = least_squares(lambda q: np.concatenate([(model(q) - h)[m].real, (model(q) - h)[m].imag]), x0)
    err = np.abs(20 * np.log10(np.abs(model(r.x)[m]) / np.abs(h[m]))).max()
    p = pole(r.x)
    return float(2 * SR * (1 - p) / (1 + p) / (2 * np.pi)), float(err)


def main():
    knobs = [t for t in range(128) if t != 64]
    specs = [dict(native=noise_patch(), notes=((0, LEN, 60, 127),), length=LEN)]
    specs += [dict(native=noise_patch(p60=t), notes=((0, LEN, 60, 127),), length=LEN) for t in knobs]
    outs = s1.render_many(specs)
    x = outs[0][0][0].astype(float)
    hz = {64: 22000.0}
    for t, (y, _) in zip(knobs, outs[1:]):
        hz[t], err = fit(tf(x, y[0].astype(float)), t)
        print(f"tone {t:3d}: {'LP' if t < 64 else 'HP'} {hz[t]:9.1f} Hz  (max err {err:.3f} dB)")
    (s1.ROOT / "reference/meta/eq_tone.json").write_text(json.dumps({"hz": [hz[t] for t in range(128)]}, indent=1))


if __name__ == "__main__":
    main()
