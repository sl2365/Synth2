"""Candidate digital filter models, evaluated on a frequency grid."""
import numpy as np

SR = 44100


def zgrid(freqs, sr=SR):
    return np.exp(-2j * np.pi * np.asarray(freqs) / sr)


def biquad_bilinear(freqs, fc, q, kind="lp", prewarp=True, sr=SR):
    """RBJ-style analog prototype through the bilinear transform."""
    w = 2 * sr * np.tan(np.pi * fc / sr) if prewarp else 2 * np.pi * fc
    K = 2 * sr
    z1 = zgrid(freqs, sr)
    s = K * (1 - z1) / (1 + z1)
    den = s * s / (w * w) + s / (q * w) + 1
    num = {"lp": 1.0, "hp": s * s / (w * w), "bp": s / (q * w), "bp0": s / w}[kind]
    return num / den


def svf_chamberlin(freqs, fc, q, kind="lp", sr=SR):
    """Chamberlin state-variable filter (lp/bp/hp taps), closed form."""
    f = 2 * np.sin(np.pi * fc / sr)
    d = 1.0 / q
    z1 = zgrid(freqs, sr)
    # low[n] = low[n-1] + f*band[n-1]; high = x - low[n] - d*band[n-1]; band = f*high + band[n-1]
    den = 1 - (2 - f * d - f * f) * z1 + (1 - f * d) * z1 * z1
    lp = f * f * z1 / den
    bp = f * (1 - z1) / den
    hp = (1 - z1) ** 2 / den
    return {"lp": lp, "bp": bp, "hp": hp}[kind]


def onepole_tpt(freqs, fc, sr=SR):
    g = np.tan(np.pi * fc / sr)
    z1 = zgrid(freqs, sr)
    return g * (1 + z1) / ((1 + g) - (1 - g) * z1)


def impulse_tf(step, freqs, n=16384, sr=SR):
    """Transfer function of a per-sample `step(x) -> y` from its impulse response."""
    h = np.zeros(n)
    for i in range(n):
        h[i] = step(1.0 if i == 0 else 0.0)
    H = np.fft.rfft(h)
    f = np.fft.rfftfreq(n, 1 / sr)
    return np.interp(freqs, f, H.real) + 1j * np.interp(freqs, f, H.imag)


def chamberlin_os(freqs, fc, q, kind="lp", os=2, sr=SR):
    """Chamberlin SVF iterated `os` times per sample with the input held."""
    f = 2 * np.sin(np.pi * fc / (sr * os))
    d = 1.0 / q
    st = {"low": 0.0, "band": 0.0}

    def step(x):
        for _ in range(os):
            low = st["low"] + f * st["band"]
            high = x - low - d * st["band"]
            band = f * high + st["band"]
            st["low"], st["band"] = low, band
        return {"lp": low, "bp": band, "hp": high}[kind]

    return impulse_tf(step, freqs, sr=sr)


def svf_tpt(freqs, fc, q, kind="lp", sr=SR):
    """Zavalishin TPT SVF (== prewarped bilinear biquad)."""
    return biquad_bilinear(freqs, fc, q, kind, True, sr)


def ladder_naive(freqs, g, k, os=1, gain=1.0, sr=SR):
    """4 x (y += g*(x - y)) with unit-delay global feedback, `os` iterations/sample (input held)."""
    z = np.exp(-2j * np.pi * np.asarray(freqs) / sr)
    # per iteration: stage transfer in the oversampled domain is LTI; simulate via impulse
    st = [0.0] * 4

    def step(x):
        for _ in range(os):
            u = gain * x - k * st[3]
            for i in range(4):
                st[i] += g * (u - st[i])
                u = st[i]
        return st[3]

    return impulse_tf(step, freqs, sr=sr)


def ladder_tpt(freqs, fc, k, gain=1.0, sr=SR):
    """Zero-delay-feedback (TPT) linear ladder: G^4 / (1 + k G^4)."""
    G = onepole_tpt(freqs, fc, sr)
    G4 = G ** 4
    return gain * G4 / (1 + k * G4)


def ladder_tpt_delayfb(freqs, fc, k, gain=1.0, os=1, sr=SR):
    """Bilinear one-pole stages, global feedback through one (oversampled) sample delay."""
    srr = sr * os
    g = np.tan(np.pi * fc / srr)
    a = g / (1 + g)
    st = [0.0] * 4
    fb = [0.0]

    def step(x):
        for _ in range(os):
            u = gain * x - k * fb[0]
            for i in range(4):
                v = (u - st[i]) * a
                y = v + st[i]
                st[i] = y + v
                u = y
            fb[0] = u
        return u

    return impulse_tf(step, freqs, sr=sr)
