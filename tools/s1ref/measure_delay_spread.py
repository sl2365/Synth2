#!/usr/bin/env python3
"""Delay spread (83): L/R echo lag (cross-correlation, sub-sample) for every
knob value. Usage: measure_delay_spread.py [ours]. Writes
reference/meta/delay_spread.json (original only)."""
import json
import sys
import numpy as np, s1
SR = 44100
OURS = sys.argv[1:] == ["ours"]
V = list(range(128))
specs = [dict(native=s1.patch(p65=1, p35=12, p36=0, p37=127, p83=v, p1=4, p5=127), notes=((0, 0.05, 57, 127),), length=0.8) for v in V]
def lag(x):
    a = np.asarray(x[0][int(0.45*SR):int(0.75*SR)], float); b = np.asarray(x[1][int(0.45*SR):int(0.75*SR)], float)
    n = len(a); A = np.fft.rfft(a, 2*n); B = np.fft.rfft(b, 2*n); c = np.fft.irfft(A * np.conj(B)); c = np.concatenate([c[-n:], c[:n]])
    i = int(np.argmax(c)); y0, y1, y2 = c[i-1], c[i], c[i+1]; d = 0.5 * (y0 - y2) / (y0 - 2*y1 + y2)
    return (i + d - n) / SR * 1000   # >0: L later
o = [x for x, _ in s1.render_many(specs, ours=OURS)]
lags = [lag(x) for x in o]
if not OURS: json.dump({"v": V, "lag_ms": lags}, open(s1.ROOT / "reference/meta/delay_spread.json", "w"))
print(" ".join(f"{v}:{l:.2f}" for v, l in zip(V, lags)))
