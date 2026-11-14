#!/usr/bin/env python3
"""Osc2 noise generator and voice lifetime.

  measure_noise.py

Noise alone (osc2 noise, mix 127, HP12 at cutoff 0 = nearly flat), the
fixed output lowpass undone (bilinear one-pole, exact inverse): the
original's noise is a 1-BIT sequence (two levels, +-0.6504 internally).
Berlekamp-Massey finds its linear recurrence over GF(2):
    s[n] = s[n-13] ^ s[n-18] ^ s[n-19] ^ s[n-20]
from s[0..19] = 16 zeros, 1, 0, 1, 0 (Voice.hpp kNoiseSeed = the 20 bits
before). A voice slot keeps its sequence across notes (frozen while idle),
a synced osc1 wrap restarts it. The sequence position at a later note is
then a sample counter of the slot's lifetime: a released voice ends at the
first 256-sample boundary (host block relative) after its envelope falls
under -66 dB (Adsr kSilence).
"""
import numpy as np

import s1

SR = 44100
K = np.pi * 22000 / SR
A, B = K / (1 + K), (1 - K) / (1 + K)
NOISE = s1.patch(p1=4, p5=127, p14=2, p19=0, p20=0, p21=64, p25=0, p27=127, p91=1, p28=0, p38=1)


def unfilter(y):
    x, xp, yp = np.zeros(len(y)), 0.0, 0.0
    for n, v in enumerate(y):
        x[n] = (v - B * yp) / A - xp
        xp, yp = x[n], v
    return x


def bits(x, w=33):
    pad = np.pad(x, (w // 2, w // 2), mode="edge")
    mid = np.array([0.5 * (pad[i:i + w].max() + pad[i:i + w].min()) for i in range(len(x))])
    return (x > mid).astype(np.uint8)


def berlekamp_massey(s):
    n = len(s)
    c, b = np.zeros(n, np.uint8), np.zeros(n, np.uint8)
    c[0] = b[0] = 1
    length, m = 0, -1
    for i in range(n):
        d = s[i]
        for j in range(1, length + 1):
            d ^= c[j] & s[i - j]
        if d:
            t = c.copy()
            c[i - m:] ^= b[:n - (i - m)]
            if 2 * length <= i:
                length, m, b = i + 1 - length, i, t
    return length, [j for j in range(1, length + 1) if c[j]]


def reference(n):
    s = [0] * 16 + [1, 0, 1, 0]
    while len(s) < n:
        i = len(s)
        s.append(s[i - 13] ^ s[i - 18] ^ s[i - 19] ^ s[i - 20])
    return "".join(map(str, s))


def main():
    (y, _), = s1.render_many([{"native": NOISE, "notes": ((0, 0.4, 60, 127),), "length": 0.4}])
    x = unfilter(y[0].astype(float))
    s = np.concatenate([np.zeros(16, np.uint8), bits(x[16:16 + 12000])])  # 0..15: the attack ramp, all low
    length, taps = berlekamp_massey(s[:2000])
    bad = sum(s[i] != (s[i - 13] ^ s[i - 18] ^ s[i - 19] ^ s[i - 20]) for i in range(20, len(s)))
    print(f"linear complexity {length}, taps {taps}; recurrence violations {bad} of {len(s) - 20}")
    flips = np.diff(s[16:]) != 0
    print(f"level +-{np.median(np.abs(np.diff(x[16:16 + 12000]))[flips[:11999]]) / 2 / 0.2:.4f} (internal)")
    ref = reference(2_000_000)
    rels = [0, 20, 40, 60]
    specs = [{"native": {**NOISE, 28: r}, "notes": ((0, 0.3, 60, 127), (12.0, 12.3, 64, 127)), "length": 12.3}
             for r in rels]
    for ours in (False, True):
        out = []
        for r, (y, _) in zip(rels, s1.render_many(specs, ours=ours)):
            i2 = int(12.0 * SR) // 16 * 16
            b = bits(unfilter(y[0][i2:i2 + 3000].astype(float)))
            out.append(f"rel {r}: {ref.find(''.join(map(str, b[100:180]))) - 100 - 13216}")
        print(("ours" if ours else "orig"), "slot lifetime after note-off:", ", ".join(out))


if __name__ == "__main__":
    main()
