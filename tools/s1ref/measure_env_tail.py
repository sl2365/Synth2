#!/usr/bin/env python3
"""Envelope tails: decay / release land in finite time.

  measure_env_tail.py

Decay to sustain 0 (sine, Hilbert envelope): the original reaches -20 dB
when a pure exponential with the kEnvTauSeconds tau does, but -60 dB ~7 %
earlier; a fit l = (1 + e) exp(-t / tau) - e gives e ~ 5e-4 for every slow
decay. Adsr: decay / release aim 1/2048 below their target and stop on it
(the release end then matches measure_noise.py's slot lifetimes exactly:
tau ln(2049) = 7.63 tau). Prints the time to each level, original / ours.
"""
import numpy as np
from scipy.signal import hilbert

import s1

SR = 44100
LEVELS = (-10, -20, -30, -40, -50, -60)
LENGTHS = {32: 1.0, 48: 2.0, 64: 4.0, 80: 9.0}


def times(x):
    e = np.abs(hilbert(x[0].astype(float)))
    e /= e[100:1500].max()
    db = 20 * np.log10(np.maximum(e, 1e-12))
    start = int(np.argmax(e[:3000]))
    return np.array([(start + np.argmax(db[start:] < lv)) / SR for lv in LEVELS])


def main():
    specs = [{"native": s1.patch(p0=0, p91=1, p26=v, p27=0, p28=0), "notes": ((0, 20.0, 69, 127),), "length": n}
             for v, n in LENGTHS.items()]
    ref, ours = s1.render_many(specs), s1.render_many(specs, ours=True)
    print("decay   " + " ".join(f"{lv:>8d}" for lv in LEVELS) + "  dB: time ms, original / ours")
    for v, (a, _), (b, _) in zip(LENGTHS, ref, ours):
        ta, tb = times(a), times(b)
        print(f"{v:5d}   " + " ".join(f"{x * 1000:8.1f}" for x in ta))
        print("        " + " ".join(f"{x / y:8.4f}" for x, y in zip(ta, tb)))


if __name__ == "__main__":
    main()
