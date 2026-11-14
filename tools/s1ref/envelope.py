"""Amplitude-envelope extraction from a steady sine carrier."""
import numpy as np
from scipy.signal import fftconvolve


def demod(x, sr, freq, smooth_periods=1.0):
    """|analytic| envelope of channel 0 at carrier `freq`, boxcar-smoothed
    over `smooth_periods` carrier periods (removes the 2f term exactly)."""
    s = np.asarray(x[0], dtype=np.float64)
    t = np.arange(len(s)) / sr
    z = s * np.exp(-2j * np.pi * freq * t)
    n = max(1, int(round(sr / freq * smooth_periods)))
    k = np.ones(n) / n
    return 2 * np.abs(fftconvolve(z, k, mode="same"))


def crossing(env, level, start=0, falling=False):
    """First index >= start where env crosses `level` (linear interpolation)."""
    e = env[start:]
    idx = np.nonzero(e <= level if falling else e >= level)[0]
    if len(idx) == 0:
        return None
    i = idx[0]
    if i == 0:
        return float(start)
    a, b = e[i - 1], e[i]
    return start + i - 1 + (level - a) / (b - a) if b != a else float(start + i)
