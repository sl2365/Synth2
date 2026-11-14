#!/usr/bin/env python3
"""Osc mix law with the sub on (osc1 sine, osc2 triangle a fifth up, sub sine
an octave down): weights of sub / osc1 / osc2 from a joint least-squares fit.

  measure_osc_mix.py [--ours]

Measured on the original: with sub weight w = g / (g + 32) and m = mix / 127,
osc1 (1-w)(1-m), sub w(1-m), osc2 (1-w)m, all over 1 - wm (sub off = the
plain linear crossfade). Prints measured next to that law.
"""
import sys

import numpy as np

import s1

SR = 44100
KEY = 60
F1 = s1.key_hz(KEY)
FREQS = (F1 / 2, F1, s1.key_hz(KEY + 7))
TRI = 8 / np.pi ** 2  # triangle fundamental


def weights(x):
    m = np.asarray(x[0][int(.3 * SR):int(1.3 * SR)], float)
    t = np.arange(len(m)) / SR
    M = np.stack([g(2 * np.pi * f * t) for f in FREQS for g in (np.cos, np.sin)] + [np.ones_like(t)], 1)
    c, *_ = np.linalg.lstsq(M, m, rcond=None)
    a = np.hypot(c[0:6:2], c[1:6:2]) / 0.2
    a[2] /= TRI
    return a


def law(g, mix):
    w, m = g / (g + 32), mix / 127
    n = 1 - w * m
    return np.array([w * (1 - m), (1 - w) * (1 - m), (1 - w) * m]) / n


def main():
    cases = [(g, mix) for g in (0, 32, 100) for mix in (0, 16, 32, 64, 96, 112, 127)]
    specs = [dict(native=s1.patch(p0=0, p1=3, p2=71, p95=g, p96=0, p97=1, p5=mix, p91=1), notes=((0, 1.5, KEY, 127),),
                  length=1.5) for g, mix in cases]
    renders = s1.render_many(specs, ours="--ours" in sys.argv)
    worst = 0
    print("sub mix | measured sub osc1 osc2 | law")
    for (g, mix), (x, _) in zip(cases, renders):
        a, b = weights(x), law(g, mix)
        worst = max(worst, np.abs(a - b).max())
        print(f"{g:3d} {mix:3d} | {a[0]:.3f} {a[1]:.3f} {a[2]:.3f} | {b[0]:.3f} {b[1]:.3f} {b[2]:.3f}")
    print(f"max |measured - law| {worst:.4f}")


if __name__ == "__main__":
    main()
