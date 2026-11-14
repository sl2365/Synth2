"""Effective cutoff (in LP12 knob units) of a render, from the identical osc2
noise: H = Pxy/Pxx against the flat reference, fit the SVF coefficient f,
invert fc = 16.7 * 2^(c / 12.624)."""
import numpy as np
from scipy.optimize import minimize_scalar
from scipy.signal import csd, welch

import fit_svf as S
import s1

SR = 44100
_REF = {}


def noise_patch(**kw):
    p = s1.patch(p0=2, p8=0, p1=4, p5=127, p14=0, p20=0, p21=64)
    for k, v in kw.items():
        p[int(k.lstrip("p"))] = v
    return p


def reference(length, key=60):
    if length not in _REF:
        (x, _), = s1.render_many([dict(native=noise_patch(p14=2, p19=0), notes=((0, length, key, 127),), length=length)])
        _REF[length] = x[0].astype(np.float64)
    return _REF[length]


def knob_from_h(f, h):
    mag = 20 * np.log10(np.abs(h) + 1e-12)
    keep = (mag > -40) & (f > 60) & (f < 18000)

    def err(c):
        hz = 16.7 * 2 ** (c / 12.624)
        ff = 2 * np.sin(np.pi * min(hz, SR) / (2 * SR))
        m = S.svf2(f[keep], ff, 1.0, "lp")
        return float(np.sum((20 * np.log10(np.abs(m) + 1e-12) - mag[keep]) ** 2))

    r = minimize_scalar(err, bounds=(-30, 140), method="bounded", options={"xatol": 1e-3})
    return r.x


def cutoff_knob(x, start, stop, length):
    ref = reference(length)
    seg = slice(int(start * SR), int(stop * SR))
    f, pxx = welch(ref[seg], SR, nperseg=2048)
    f, pxy = csd(ref[seg], np.asarray(x[0], float)[seg], SR, nperseg=2048)
    return knob_from_h(f, pxy / pxx)
