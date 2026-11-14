#!/usr/bin/env python3
"""Chorus, original vs ours. Identical-noise trick: osc2 white noise (osc1
silenced), panned hard left then hard right, against the same render with
the chorus off -> the 2x2 L/R impulse-response matrix of the chorus
(static: depth 0). Helpers are imported by the other chorus probes.
Usage: measure_chorus.py   (prints taps for each type)."""
import numpy as np
from scipy import signal

import s1

SR = 44100
L = 2.0
NOISE = dict(p0=2, p8=0, p5=127, p1=4)
IRLEN = int(0.25 * SR)


def chorus(**kw):
    base = dict(p66=1, p64=2, p52=64, p53=0, p54=0, p55=64, p56=127)
    base.update(kw)
    return base


def irs(specs_kw, ours=False):
    """specs_kw: list of chorus param dicts. Returns list of 2x2 IR arrays
    ir[src][dst] (src 0 = input left, 1 = right)."""
    specs = []
    for kw in specs_kw:
        for pan in (0, 127):
            specs.append(dict(native=s1.patch(p90=pan, **NOISE, **kw), notes=((0, L, 60, 127),), length=L))
    for pan in (0, 127):
        specs.append(dict(native=s1.patch(p90=pan, **NOISE), notes=((0, L, 60, 127),), length=L))
    outs = [x for x, _ in s1.render_many(specs, ours=ours)]
    ref = outs[-2:]
    res = []
    for i in range(len(specs_kw)):
        m = np.zeros((2, 2, IRLEN))
        for src in (0, 1):
            x = np.asarray(ref[src][src][int(0.1 * SR):], float)
            y2 = outs[2 * i + src]
            for dst in (0, 1):
                y = np.asarray(y2[dst][int(0.1 * SR):], float)
                f, pxy = signal.csd(x, y, SR, nperseg=32768)
                _, pxx = signal.welch(x, SR, nperseg=32768)
                h = np.fft.irfft(pxy / pxx)
                m[src, dst] = h[:IRLEN]
        res.append(m)
    return res


def taps(h, thr=0.02, n=8):
    """(delay ms, gain) of the strongest peaks, sub-sample."""
    a = np.abs(h)
    idx = [i for i in range(1, len(h) - 1) if a[i] >= a[i - 1] and a[i] > a[i + 1] and a[i] > thr]
    idx = sorted(idx, key=lambda i: -a[i])[:n]
    out = []
    for i in sorted(idx):
        y0, y1, y2 = h[i - 1], h[i], h[i + 1]
        den = y0 - 2 * y1 + y2
        d = 0.5 * (y0 - y2) / den if den else 0.0
        out.append((round((i + d) / SR * 1000, 3), round(float(y1 - 0.25 * (y0 - y2) * d), 3)))
    return out


def main():
    kws = [chorus(p64=t) for t in (1, 2, 3, 4)]
    for ours in (False, True):
        print("ours" if ours else "s1")
        for t, m in zip((1, 2, 3, 4), irs(kws, ours)):
            print(f"  type {t}: " + "  ".join(f"{'LR'[s]}->{'LR'[d]} {taps(m[s, d])}" for s in (0, 1) for d in (0, 1)))


if __name__ == "__main__":
    main()


def render_pair(kw, pan=0, ours=False, length=L):
    """(input x, outputs y) for one chorus setting: x = the chorus-off render."""
    specs = [dict(native=s1.patch(p90=pan, **NOISE, **kw), notes=((0, length, 60, 127),), length=length),
             dict(native=s1.patch(p90=pan, **NOISE), notes=((0, length, 60, 127),), length=length)]
    y, x = [o for o, _ in s1.render_many(specs, ours=ours)]
    src = 0 if pan == 0 else 1
    return np.asarray(x[src], float), [np.asarray(y[c], float) for c in (0, 1)]


def track(x, w, win=0.02, step=0.01, maxd=0.2, ntaps=1, t0=0.25):
    """Delay (ms) / gain of the ntaps strongest taps of w vs x over time."""
    n, md = int(win * SR), int(maxd * SR)
    out = []
    for a in range(int(t0 * SR), len(w) - n, int(step * SR)):
        seg = w[a:a + n]
        ref = x[a - md:a + n]
        c = signal.correlate(seg, ref, mode="valid")[::-1] / (np.dot(ref[md:], ref[md:]) + 1e-12)
        picks = []
        cc = c.copy()
        for _ in range(ntaps):
            i = int(np.argmax(np.abs(cc)))
            if 0 < i < len(cc) - 1:
                y0, y1, y2 = cc[i - 1], cc[i], cc[i + 1]
                den = y0 - 2 * y1 + y2
                d = 0.5 * (y0 - y2) / den if den else 0.0
            else:
                d = 0.0
            picks.append(((md - i - d) / SR * 1000, float(cc[i])))
            cc[max(0, i - 20):i + 21] = 0
        out.append((a / SR, sorted(picks)))
    return out


SINE = dict(p0=0, p5=0)
SINE_KEY = 84


def delay_curve(kw, pan=0, ours=False, length=4.0, ch=None):
    """Wet delay d(t) in ms of a 1 kHz sine through the chorus (phase
    demodulation of wet vs dry). kw must give one voice per channel."""
    from scipy.signal import hilbert
    f0 = s1.key_hz(SINE_KEY)
    specs = [dict(native=s1.patch(p90=pan, **SINE, **kw), notes=((0, length, SINE_KEY, 127),), length=length),
             dict(native=s1.patch(p90=pan, **SINE), notes=((0, length, SINE_KEY, 127),), length=length)]
    y, x = [o for o, _ in s1.render_many(specs, ours=ours)]
    src = 0 if pan == 0 else 1
    ch = src if ch is None else ch
    a = int(0.05 * SR)
    xs = np.asarray(x[src][a:], float)
    w = np.asarray(y[ch][a:], float) - (xs if ch == src else 0)
    ph = np.unwrap(np.angle(hilbert(w) * np.conj(hilbert(xs))))
    d = -ph / (2 * np.pi * f0) * 1000
    return (np.arange(len(d)) + a) / SR, d, np.abs(hilbert(w)) / (np.abs(hilbert(xs)) + 1e-12)
