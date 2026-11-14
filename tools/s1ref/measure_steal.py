#!/usr/bin/env python3
"""Voice stealing at the polyphony limit (poly 2): which notes still sound
after a third note, with all keys held / with the oldest released (long
release), and how fast a stolen voice goes. Original vs ours.
Usage: measure_steal.py [ours]."""
import sys

import numpy as np

import s1

SR = 44100
OURS = sys.argv[1:] == ["ours"]
KEYS = (60, 64, 67)


def levels(x, a, b):
    s = np.asarray(x[0][int(a * SR):int(b * SR)], float)
    t = np.arange(len(s)) / SR
    out = []
    for k in KEYS:
        f = s1.key_hz(k)
        out.append(float(np.hypot(np.dot(s, np.cos(2 * np.pi * f * t)), np.dot(s, np.sin(2 * np.pi * f * t))) * 2 / len(s)))
    return out


def main():
    base = dict(p0=0, p94=2, p28=90)  # sine, poly 2, long release
    cases = {
        "all held": ((0.0, 2.0, 60, 127), (0.3, 2.0, 64, 127), (0.6, 2.0, 67, 127)),
        "60 released at 0.4": ((0.0, 0.4, 60, 127), (0.3, 2.0, 64, 127), (0.6, 2.0, 67, 127)),
        "64 released at 0.4": ((0.0, 2.0, 60, 127), (0.3, 0.4, 64, 127), (0.6, 2.0, 67, 127)),
    }
    outs = s1.render_many([dict(native=s1.patch(**base), notes=n, length=2.0) for n in cases.values()], ours=OURS)
    for name, (x, _) in zip(cases, outs):
        print(f"{name:20s}: levels 60/64/67 before {np.round(levels(x, 0.45, 0.6), 3).tolist()} after "
              f"{np.round(levels(x, 0.8, 1.0), 3).tolist()}")
        s = np.abs(np.asarray(x[0], float))
        env = [float(s[int(t * SR):int(t * SR) + 220].max()) for t in np.arange(0.59, 0.62, 0.002)]
        print("   peak around 0.6 s:", " ".join(f"{v:.3f}" for v in env))


if __name__ == "__main__":
    main()
