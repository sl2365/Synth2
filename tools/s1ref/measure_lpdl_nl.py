#!/usr/bin/env python3
"""LPDL (filter type 4) level dependence of the original.

1. noise: complex transfer functions (identical-noise trick, see
   measure_filter.py) at 5 input levels (osc mix 127 / 64 / 30 / 10 / 4 =
   noise rms ~0.10 .. 0.004) over a cutoff x resonance grid.
2. sine: osc1 sine (key 36, 65.4 Hz) at 6 levels set by osc mix; osc2 is a
   pulse at pw 0 (NOT silent: a narrow pulse, ~0.002 per harmonic) with kbd
   track off (fixed 220 Hz, off the sine's harmonic grid) -> output harmonics
   1..7 (amplitude, phase) per level, same grid.
Writes reference/derived/lpdl_nl.npz (gitignored)."""
import numpy as np
from scipy.signal import csd, welch

import s1

SR = 44100
LEN = 2.0
FREQS = np.geomspace(30, 16000, 160)
NOISE_MIX = [127, 64, 30, 10, 4]
SINE_MIX = [0, 32, 64, 96, 112, 120]
CUTS = [30, 50, 70, 90, 110, 124]
RESS = [0, 32, 64, 96, 112, 127]
KEY = 36


def tf(x, y):
    seg = slice(int(0.15 * SR), int((LEN - 0.05) * SR))
    f, pxx = welch(x[seg], SR, nperseg=4096)
    f, pxy = csd(x[seg], y[seg], SR, nperseg=4096)
    h = pxy / pxx
    return np.interp(FREQS, f, h.real) + 1j * np.interp(FREQS, f, h.imag)


def noise_spec(mix, **kw):
    return dict(native=s1.patch(p0=2, p8=0, p1=4, p5=mix, **kw), notes=((0, LEN, 60, 127),), length=LEN)


def sine_spec(mix, **kw):
    return dict(native=s1.patch(p0=0, p1=2, p8=0, p4=0, p5=mix, **kw), notes=((0, 1.0, KEY, 127),), length=1.0)


def main(ours=False):
    f0 = s1.key_hz(KEY)
    refs = [o[0][0].astype(np.float64) for o in s1.render_many([noise_spec(m, p14=2, p19=0, p20=0) for m in NOISE_MIX], ours=ours)]
    sref = s1.render_many([sine_spec(m, p14=2, p19=0, p20=0) for m in SINE_MIX], ours=ours)
    sine_in = np.array([s1.tone_amp(x, sr, f0) for x, sr in sref])
    H = np.zeros((len(CUTS), len(RESS), len(NOISE_MIX), len(FREQS)), np.complex128)
    S = np.zeros((len(CUTS), len(RESS), len(SINE_MIX), 7), np.complex128)
    for ci, c in enumerate(CUTS):
        specs = [noise_spec(m, p14=4, p19=c, p20=r) for r in RESS for m in NOISE_MIX]
        specs += [sine_spec(m, p14=4, p19=c, p20=r) for r in RESS for m in SINE_MIX]
        outs = s1.render_many(specs, ours=ours)
        k = 0
        for ri in range(len(RESS)):
            for mi in range(len(NOISE_MIX)):
                H[ci, ri, mi] = tf(refs[mi], outs[k][0][0].astype(np.float64))
                k += 1
        for ri in range(len(RESS)):
            for mi in range(len(SINE_MIX)):
                x, sr = outs[k]
                hs, a, ph = s1.harmonics(x, sr, f0, 7, start=0.5, dur=0.4)
                S[ci, ri, mi, : len(a)] = a * np.exp(1j * ph)
                k += 1
        print("cutoff", c, "done", flush=True)
    noise_rms = np.array([np.sqrt(np.mean(r[int(0.15 * SR):] ** 2)) for r in refs])
    out = s1.ROOT / "reference/derived"
    out.mkdir(parents=True, exist_ok=True)
    name = "lpdl_nl_ours.npz" if ours else "lpdl_nl.npz"
    np.savez_compressed(out / name, H=H, S=S, freqs=FREQS, cuts=CUTS, ress=RESS, noise_mix=NOISE_MIX,
                        noise_rms=noise_rms, sine_mix=SINE_MIX, sine_in=sine_in, key=KEY)
    print("wrote", out / name, "noise rms", np.round(noise_rms, 4), "sine in", np.round(sine_in, 4))


if __name__ == "__main__":
    import sys
    main(ours="--ours" in sys.argv)
