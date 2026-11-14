#!/usr/bin/env python3
"""MIDI ctrl routing with pitch bend (0xE000) and channel pressure (0xD000)
as sources, onto filter freq (cutoff_probe calibration as measure_wheel.py),
original vs ours. Usage: measure_ctrl_sources.py [ours]."""
import sys

import numpy as np

import cutoff_probe as C
import s1

L = 1.2
OURS = sys.argv[1:] == ["ours"]
BENDS = [0, 1024, 4096, 6144, 8064, 8192, 8320, 10240, 12288, 16383]
PRESS = [0, 1, 16, 32, 64, 96, 127]


def probe(specs):
    return [C.cutoff_knob(x, 0.3, L - 0.05, L) for x, _ in s1.render_many(specs, ours=OURS)]


def spec(**kw):
    ev = {k: kw.pop(k) for k in ("bends", "press") if k in kw}
    return dict(native=C.noise_patch(**kw), notes=((0.05, L, 60, 127),), length=L, p40=0, **ev) if False else \
        dict(native=C.noise_patch(p40=0, **kw), notes=((0.05, L, 60, 127),), length=L, **ev)


def main():
    calib = np.array(probe([spec(p19=k) for k in range(128)]))
    knob = lambda m: int(np.argmin(np.abs(calib - m)))
    for sens in (127, 96, 0):
        specs = [spec(p19=64, p86=0xE000, p87=19, p50=sens, p88=0xB002, p89=19, p51=64, bends=((0.0, b),)) for b in BENDS]
        print(f"bend sens {sens}:", " ".join(f"{b}:{knob(m) - 64:+d}" for b, m in zip(BENDS, probe(specs))))
        specs = [spec(p19=64, p86=0xD000, p87=19, p50=sens, p88=0xB002, p89=19, p51=64, press=((0.0, v),)) for v in PRESS]
        print(f"press sens {sens}:", " ".join(f"{v}:{knob(m) - 64:+d}" for v, m in zip(PRESS, probe(specs))))


if __name__ == "__main__":
    main()
