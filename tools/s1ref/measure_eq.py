#!/usr/bin/env python3
"""Equalizer peak section (60..63) of the original: complex transfer
functions from identical noise (see measure_filter.py), each fitted with an
RBJ peaking biquad (f0, Q, gain dB; least squares on the complex response).

Sweeps (tone 64): freq 0..127 (level 127, Q 64), level 0..127 (freq 64,
Q 64), Q 0..127 at level 127 and at level 0 (freq 64).
Writes reference/meta/eq.json: per sweep the knob values and fitted
f0 / Q / dB + the fit's rms error (dB)."""
import json

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import csd, welch

import s1

SR = 44100
LEN = 2.0
MIX = 30
FREQS = np.geomspace(25, 18000, 200)
KNOBS = list(range(0, 128, 4)) + [127]


def noise_patch(**kw):
    return s1.patch(p0=2, p8=0, p1=4, p5=MIX, p14=2, p19=0, p20=0, **kw)


def tf(x, y):
    seg = slice(int(0.15 * SR), int((LEN - 0.05) * SR))
    f, pxx = welch(x[seg], SR, nperseg=8192)
    f, pxy = csd(x[seg], y[seg], SR, nperseg=8192)
    h = pxy / pxx
    return np.interp(FREQS, f, h.real) + 1j * np.interp(FREQS, f, h.imag)


def rbj(f0, q, db, freqs=FREQS, sr=SR):
    A = 10 ** (db / 40)
    w = 2 * np.pi * min(f0, sr * 0.49) / sr
    al, c = np.sin(w) / (2 * q), np.cos(w)
    b = np.array([1 + al * A, -2 * c, 1 - al * A])
    a = np.array([1 + al / A, -2 * c, 1 - al / A])
    z = np.exp(-1j * 2 * np.pi * freqs / sr)
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def fit(h):
    mag = 20 * np.log10(np.abs(h))
    k = int(np.argmax(np.abs(mag)))
    x0 = [np.log(FREQS[k]), np.log(1.0), mag[k]]

    def res(p):
        m = rbj(np.exp(p[0]), np.exp(p[1]), p[2])
        return np.concatenate([(m - h).real, (m - h).imag])

    r = least_squares(res, x0)
    f0, q, db = float(np.exp(r.x[0])), float(np.exp(r.x[1])), float(r.x[2])
    err = float(np.sqrt(np.mean((20 * np.log10(np.abs(rbj(f0, q, db)) / np.abs(h))) ** 2)))
    return f0, q, db, err


def main():
    (ref, _), = s1.render_many([dict(native=noise_patch(), notes=((0, LEN, 60, 127),), length=LEN)])
    x = ref[0].astype(np.float64)
    sweeps = {
        "freq": [dict(p61=v, p62=127, p63=64) for v in KNOBS],
        "level": [dict(p61=64, p62=v, p63=64) for v in KNOBS],
        "q_boost": [dict(p61=64, p62=127, p63=v) for v in KNOBS],
        "q_cut": [dict(p61=64, p62=0, p63=v) for v in KNOBS],
    }
    out, raw = {"knobs": KNOBS}, {}
    for name, kws in sweeps.items():
        outs = s1.render_many([dict(native=noise_patch(**kw), notes=((0, LEN, 60, 127),), length=LEN) for kw in kws])
        rows, hs = [], []
        for kw, (y, _) in zip(kws, outs):
            h = tf(x, y[0].astype(np.float64))
            hs.append(h)
            rows.append(fit(h) if abs(kw["p62"] - 64) > 0 else (0.0, 0.0, 0.0, 0.0))
        raw[name] = np.array(hs)
        out[name] = rows
        for v, (f0, q, db, err) in zip(KNOBS, rows):
            print(f"{name:8s} {v:3d}: f0 {f0:8.1f} Hz  Q {q:6.3f}  {db:+6.2f} dB  (fit err {err:.2f} dB)")
    path = s1.ROOT / "reference/meta/eq.json"
    path.write_text(json.dumps(out, indent=1))
    derived = s1.ROOT / "reference/derived"
    derived.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(derived / "eq_tf.npz", freqs=FREQS, knobs=np.array(KNOBS), **raw)
    print("wrote", path, "+ reference/derived/eq_tf.npz (raw H, gitignored)")


if __name__ == "__main__":
    main()
