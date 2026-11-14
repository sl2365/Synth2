"""Precise frequency estimation of a steady periodic signal."""
import numpy as np


def f0(x, sr, guess, start=0.3, dur=0.5, harmonics=3):
    """Refine a fundamental near `guess` (Hz) by maximizing the least-squares
    harmonic fit energy (golden-section on a +-3% bracket then parabolic)."""
    a = int(start * sr)
    s = np.asarray(x[0][a : a + int(dur * sr)], dtype=np.float64)
    s = s - s.mean()
    t = np.arange(len(s)) / sr

    def energy(f):
        cols = []
        for h in range(1, harmonics + 1):
            if h * f < sr / 2:
                cols += [np.cos(2 * np.pi * h * f * t), np.sin(2 * np.pi * h * f * t)]
        m = np.stack(cols, 1)
        c, *_ = np.linalg.lstsq(m, s, rcond=None)
        return float(np.sum((m @ c) ** 2))

    grid = guess * np.exp(np.linspace(-0.03, 0.03, 61))
    e = [energy(f) for f in grid]
    i = int(np.argmax(e))
    lo, hi = grid[max(i - 1, 0)], grid[min(i + 1, len(grid) - 1)]
    g = (np.sqrt(5) - 1) / 2
    c, d = hi - g * (hi - lo), lo + g * (hi - lo)
    ec, ed = energy(c), energy(d)
    for _ in range(40):
        if ec > ed:
            hi, d, ed = d, c, ec
            c = hi - g * (hi - lo)
            ec = energy(c)
        else:
            lo, c, ec = c, d, ed
            d = lo + g * (hi - lo)
            ed = energy(d)
    return (lo + hi) / 2


def cents(f, ref):
    return 1200 * np.log2(f / ref)
