#!/usr/bin/env python3
"""Effect types 3..9 (decimator, ring mod, compressor, phasers): the probes
behind the laws in EngineCore/Effects.hpp, original vs ours.
Usage: measure_effect_time.py [deci|rm|comp|phaser] (default all)."""
import sys

import numpy as np
from scipy.optimize import least_squares, minimize_scalar
from scipy.signal import hilbert, lfilter

import measure_effect as me

SR = 44100


def ap1(f, fc):
    z1 = np.exp(-2j * np.pi * f / SR)
    t = np.tan(np.pi * fc / SR)
    a = (t - 1) / (t + 1)
    return (a + z1) / (1 + a * z1)


def deci():
    """Hold period (= ctl1 + 1), post-hold impulse response, quantiser step."""
    for c1 in (16, 32, 64, 127):
        x, (y,) = me.many([me.fx(3, c1, 0)], dict(me.SINE, p29=64), notes=((0, 1.0, 45, 127),), length=1.0)
        P = c1 + 1
        z = x[np.arange(len(x)) // P * P]
        A = np.stack([np.roll(z, k)[20000:30000] for k in range(8)], 1)
        h, *_ = np.linalg.lstsq(A, y[20000:30000], rcond=None)
        print(f"deci c1 {c1}: h", " ".join(f"{v:+.4f}" for v in h))
    for c2 in (0, 32, 64, 72, 80, 96, 104):
        x, (y,) = me.many([me.fx(3, 0, c2)], dict(me.SINE, p29=64), notes=((0, 1.0, 45, 127),), length=1.0)
        print(f"deci c2 {c2}: q = {-2 * np.mean(y[11025:] - x[11025:]):.6f}")


def rm():
    f0 = me.s1.key_hz(84)
    for c1 in (0, 32, 64, 96, 127):
        x, (y,) = me.many([me.fx(4, c1, 0)], dict(me.SINE, p29=64), notes=((0, 2.0, 84, 127),), length=2.0)
        s = y[22050:88200]
        Y = np.abs(np.fft.rfft(s * np.hanning(len(s))))
        f = np.fft.rfftfreq(len(s), 1 / SR)
        i, j = np.argsort(Y)[-2:]
        print(f"rm c1 {c1}: modulator {max(f[i], f[j]) - f0:.2f} Hz")


def comp():
    """Detector time constant per ctl2 (model: one-pole of |x|, soft knee)."""
    base = dict(me.SINE, p29=96, p30=127, p28=0)
    notes = ((0, 0.5, 69, 127), (0.5, 2.5, 69, 30))
    C2 = [0, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127]
    x, ys = me.many([me.fx(5, 64, c) for c in C2], base, notes=notes, length=2.5)
    xe = np.abs(hilbert(x)) + 1e-9
    for c2, y in zip(C2, ys):
        gs = 20 * np.log10(np.abs(hilbert(y)) / xe)

        def cost(lt):
            a = 1 - np.exp(-1 / (10 ** lt * SR))
            env = lfilter([a], [1, -(1 - a)], np.abs(x))
            g = (10 ** (-(10 + 0.3125 * 64) / 5) + (env / 0.2604) ** 4) ** -0.25
            gm = 20 * np.log10(np.abs(hilbert(x * g)) / xe)
            sel = np.r_[200:int(0.49 * SR), int(0.5 * SR) + 200:int(2.4 * SR)]
            return float(np.mean((gm[sel] - gs[sel]) ** 2))

        r = minimize_scalar(cost, bounds=(-5, 0.5), method="bounded")
        print(f"comp c2 {c2}: tau {10 ** r.x * 1000:.3f} ms (rms {np.sqrt(r.fun):.2f} dB)")


def phaser():
    """Static stages / feedback (ctl1 0 freezes the sweep), then the sweep."""
    for t, N in ((6, 2), (7, 4), (8, 8), (9, 12)):
        x, ys = me.many([me.fx(t, 0, 0, lv) for lv in (64, 32, 96)], dict(me.NOISE, p29=64), notes=((0, 6.0, 60, 127),), length=6.0)
        for lv, y in zip((64, 32, 96), ys):
            f, H = me.transfer(x[SR:], y[SR:], nper=16384)
            sel = (f > 20) & (f < 19000)
            ff, HH = f[sel], H[sel]
            z1 = np.exp(-2j * np.pi * ff / SR)

            def err(p):
                A = ap1(ff, p[1]) ** N
                v = p[0] * (1 + A / (1 - p[2] * A * z1))
                return np.concatenate([(v - HH).real, (v - HH).imag])

            r = least_squares(err, [0.5, 250, 0.0], bounds=([0, 5, -0.999], [2, 20000, 0.999]))
            print(f"phaser type {t} lv {lv}: mix {r.x[0]:.3f} fc {r.x[1]:.1f} Hz feedback {r.x[2]:+.4f}")
    n = 1024
    f = np.fft.rfftfreq(n, 1 / SR)
    sel = (f > 30) & (f < 15000)
    for c1, c2 in ((64, 64), (127, 80)):
        x, (y,) = me.many([me.fx(6, c1, c2, 64)], dict(me.NOISE, p29=64), notes=((0, 4.0, 60, 127),), length=4.0)
        oct_ = []
        for a in range(SR, len(x) - n, 512):
            X = np.fft.rfft(x[a:a + n] * np.hanning(n))[sel]
            Y = np.fft.rfft(y[a:a + n] * np.hanning(n))[sel]
            cost = lambda lf: float(np.sum(np.abs(Y - X * 0.5 * (1 + ap1(f[sel], 10 ** lf) ** 2)) ** 2))
            grid = np.linspace(1, 4.3, 67)
            g0 = grid[int(np.argmin([cost(v) for v in grid]))]
            fc = 10 ** minimize_scalar(cost, bounds=(g0 - 0.06, g0 + 0.06), method="bounded").x
            oct_.append(np.log2(SR / np.pi * np.tan(np.pi * fc / SR) / 254.6))
        o = np.array(oct_)
        print(f"phaser c1 {c1} c2 {c2}: internal octaves {o.min():+.3f} .. {o.max():+.3f} (centre {(o.min() + o.max()) / 2:+.3f})")


if __name__ == "__main__":
    for name in sys.argv[1:] or ["deci", "rm", "comp", "phaser"]:
        globals()[name]()
