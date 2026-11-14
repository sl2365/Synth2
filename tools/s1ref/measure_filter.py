#!/usr/bin/env python3
"""Complex filter transfer functions of the original, from identical noise.

Every render in a fresh process carries the same osc2 noise sequence, so
H(f) = Pxy / Pxx (x = noise through HP12 at cutoff 0 = the flat reference,
y = noise through the filter under test) is exact up to estimation noise.
Input level is reduced (mix 30 -> -12.5 dB) to stay in the linear region;
osc1 is silenced as a zero-width pulse.

Writes reference/derived/filter_tf.npz: H[type, res_index, cutoff, freq]
(complex64), freqs, res values. Gitignored (regenerable while Rosetta lives).
"""
import numpy as np
from scipy.signal import csd, welch

import s1

SR = 44100
RES = [0, 16, 32, 48, 64, 80, 96, 112, 127]
FREQS = np.geomspace(20, 20000, 240)
MIX = 30
LEN = 2.5


def noise_patch(**kw):
    return s1.patch(p0=2, p8=0, p1=4, p5=MIX, **kw)


def tf(x, y):
    seg = slice(int(0.15 * SR), int((LEN - 0.05) * SR))
    f, pxx = welch(x[seg], SR, nperseg=8192)
    f, pxy = csd(x[seg], y[seg], SR, nperseg=8192)
    h = pxy / pxx
    return np.interp(FREQS, f, h.real) + 1j * np.interp(FREQS, f, h.imag)


def main():
    """Options: --types 4 (subset), --mix N (input level), --out name.npz
    (LPDL small-signal set: --types 4 --mix 10 --out filter_tf_lpdl_small.npz)."""
    import sys
    global MIX
    args = sys.argv[1:]
    opt = lambda k, d: args[args.index(k) + 1] if k in args else d
    types = [int(t) for t in opt("--types", "0,1,2,3,4").split(",")]
    MIX = int(opt("--mix", MIX))
    name = opt("--out", "filter_tf.npz")
    (ref, _), = s1.render_many([dict(native=noise_patch(p14=2, p19=0, p20=0), notes=((0, LEN, 60, 127),), length=LEN)])
    x = ref[0].astype(np.float64)
    H = np.zeros((5, len(RES), 128, len(FREQS)), np.complex64)
    for t in types:
        for ri, r in enumerate(RES):
            outs = s1.render_many([dict(native=noise_patch(p14=t, p19=c, p20=r), notes=((0, LEN, 60, 127),),
                                        length=LEN) for c in range(128)])
            for c, (y, _) in enumerate(outs):
                H[t, ri, c] = tf(x, y[0].astype(np.float64))
            print("type", t, "res", r, "done", flush=True)
    out = s1.ROOT / "reference/derived"
    out.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(out / name, H=H, freqs=FREQS, res=np.array(RES), mix=MIX)
    print("wrote", out / name)


if __name__ == "__main__":
    main()
