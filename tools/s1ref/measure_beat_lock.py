#!/usr/bin/env python3
"""Host-beat phase lock: tempo-synced LFO1 square (1 beat) -> amp gate and
the arpeggiator (1/8 steps), rendered with different host song positions
at the first rendered sample (beat0). Locked = edge times move with beat0.
Usage: measure_beat_lock.py [ours]."""
import sys

import numpy as np

import s1

SR = 44100
OURS = sys.argv[1:] == ["ours"]
B0 = [0.0, 0.25, 0.5, 0.75, 1.3]


def edges(x, w=int(0.0005 * SR)):
    a = np.abs(np.asarray(x[0], float))
    env = np.maximum.reduce([np.roll(a, i) for i in range(w)])
    on = env > 0.5 * env.max()
    return (np.flatnonzero(on[1:] & ~on[:-1]) + 1) / SR


def main():
    lfo = dict(p0=0, p57=1, p41=4, p42=2, p43=79, p44=127, p67=1, p68=0)
    specs = [dict(native=s1.patch(**lfo), notes=((0.1, 2.5, 96, 127),), length=2.5, beat0=b) for b in B0]
    for b, (x, _) in zip(B0, s1.render_many(specs, ours=OURS)):
        e = edges(x)
        beats = (e + 0.25) * 2 + b  # host beat of each rising edge (120 bpm, preroll 0.25)
        print(f"LFO beat0 {b:4.2f}: rising edges at host beat", " ".join(f"{v:.3f}" for v in beats[:5]))
    arp = dict(p0=0, p59=1, p31=2, p33=11, p34=64, p28=0)
    specs = [dict(native=s1.patch(**arp), notes=((0.1, 2.5, 72, 127),), length=2.5, beat0=b) for b in B0]
    for b, (x, _) in zip(B0, s1.render_many(specs, ours=OURS)):
        e = edges(x)
        beats = (e + 0.25) * 2 + b
        print(f"ARP beat0 {b:4.2f}: note onsets at host beat", " ".join(f"{v:.3f}" for v in beats[:5]))


if __name__ == "__main__":
    main()
