#!/usr/bin/env python3
"""MIDI ctrl routing law (src -> assign, sens): CC1 routed to filter freq.
Each render's effective cutoff (cutoff_probe) is mapped back to the integer
filter-freq knob by a calibration over all 128 direct knob values, giving
the exact knob offset per (sens, cc). Writes reference/meta/wheel.json."""
import json

import numpy as np

import cutoff_probe as C
import s1

L = 1.2
SENS = [0, 1, 16, 32, 48, 56, 60, 62, 63, 64, 65, 66, 68, 72, 80, 96, 112, 126, 127]
CCS = [0, 1, 8, 16, 32, 48, 63, 64, 96, 126, 127]
BASES = [40, 64]


def probe(specs):
    return [C.cutoff_knob(x, 0.3, L - 0.05, L) for x, _ in s1.render_many(specs)]


def spec(**kw):
    ccs = kw.pop("ccs", ())
    return dict(native=C.noise_patch(**kw), notes=((0.05, L, 60, 127),), length=L, ccs=ccs)


def main():
    calib = np.array(probe([spec(p19=k) for k in range(128)]))
    keys, specs = [], []
    for base in BASES:
        for sens in SENS:
            for cc in CCS:
                keys.append((base, sens, cc))
                specs.append(spec(p19=base, p86=0xB001, p87=19, p50=sens, p88=0xB002, p89=19, p51=64,
                                  ccs=((0.0, 1, cc),)))
    meas = probe(specs)
    res = {}
    for (base, sens, cc), m in zip(keys, meas):
        res[f"{base},{sens},{cc}"] = int(np.argmin(np.abs(calib - m))) - base
    path = s1.ROOT / "reference/meta/wheel.json"
    path.write_text(json.dumps(dict(target="filter_freq", sens=SENS, ccs=CCS, bases=BASES, offset=res,
                                    calib=calib.tolist()), indent=1))
    for base in BASES:
        print(f"base {base}: sens \\ cc", CCS)
        for s in SENS:
            print(f"{s:4d}", [res[f'{base},{s},{c}'] for c in CCS])


if __name__ == "__main__":
    main()
