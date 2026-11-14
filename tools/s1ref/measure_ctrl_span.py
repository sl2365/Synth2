#!/usr/bin/env python3
"""MIDI ctrl routing onto a target whose range is not 0..127: CC1 -> osc key
shift (9, -24..+24), offset read back as the osc1 pitch (semitones),
original vs ours. Usage: measure_ctrl_span.py [ours]."""
import sys

import numpy as np

import s1

SR = 44100
OURS = sys.argv[1:] == ["ours"]
CCS = [0, 8, 16, 32, 48, 64, 80, 96, 112, 127]


def semis(x):
    s = np.asarray(x[0][int(0.3 * SR):int(0.9 * SR)], float)
    S = np.abs(np.fft.rfft(s * np.hanning(len(s)), 8 * len(s)))
    f = np.fft.rfftfreq(8 * len(s), 1 / SR)
    return 12 * np.log2(f[np.argmax(S)] / s1.key_hz(60))


def main():
    for sens in (127, 96, 80, 0):
        for base in (0, -12):
            specs = [dict(native=s1.patch(p0=0, p9=base, p86=0xB001, p87=9, p50=sens, p88=0xB002, p89=9, p51=64),
                          notes=((0.05, 1.0, 60, 127),), length=1.0, ccs=((0.0, 1, c),)) for c in CCS]
            out = [semis(x) - base for x, _ in s1.render_many(specs, ours=OURS)]
            print(f"sens {sens:3d} base {base:+d}:", " ".join(f"{c}:{v:+.2f}" for c, v in zip(CCS, out)))


if __name__ == "__main__":
    main()
