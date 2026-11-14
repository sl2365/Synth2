#!/usr/bin/env python3
"""Mono / legato behaviour: amp envelope (attack 64 ~ 124 ms, sustain 64) at
a second overlapping note and at the fall-back to a still-held key, plus
the pitch after the fall-back. Original vs ours. Usage: measure_mono.py [ours]."""
import sys

import numpy as np
from scipy.signal import hilbert

import s1

SR = 44100
OURS = sys.argv[1:] == ["ours"]


def main():
    notes = ((0.0, 1.6, 60, 127), (0.6, 1.1, 67, 40))  # 67 released at 1.1 -> back to 60
    T = [0.55, 0.61, 0.65, 0.7, 0.8, 1.0, 1.12, 1.15, 1.2, 1.3, 1.5]
    for mode in (1, 2):
        x = s1.render_many([dict(native=s1.patch(p0=0, p38=mode, p25=64, p27=64, p30=127), notes=notes, length=1.6)],
                           ours=OURS)[0][0]
        s = np.asarray(x[0], float)
        e = np.abs(hilbert(s))
        ph = np.unwrap(np.angle(hilbert(s)))
        f = np.diff(ph) * SR / (2 * np.pi)
        print(f"{'mono' if mode == 1 else 'legato'}: env " + " ".join(f"{t}:{np.median(e[int(t*SR):int(t*SR)+200]):.3f}" for t in T))
        print("        key " + " ".join(f"{t}:{12*np.log2(np.median(f[int(t*SR):int(t*SR)+400])/440)+69:.1f}" for t in T))


if __name__ == "__main__":
    main()
