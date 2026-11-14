"""Instantaneous frequency of a (near) pure tone via the analytic signal."""
import numpy as np
from scipy.signal import hilbert, firwin, filtfilt


def inst_cents(x, sr, ref_hz, smooth=0.002):
    s = np.asarray(x[0], dtype=np.float64)
    a = hilbert(s)
    ph = np.unwrap(np.angle(a))
    f = np.gradient(ph) * sr / (2 * np.pi)
    n = max(3, int(smooth * sr) | 1)
    f = np.convolve(f, np.ones(n) / n, mode="same")
    return 1200 * np.log2(np.maximum(f, 1e-3) / ref_hz)
