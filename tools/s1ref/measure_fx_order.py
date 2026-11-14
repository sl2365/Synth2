#!/usr/bin/env python3
"""Effect chain order, original vs ours (black-box, order-revealing tests).
1 EQ vs effect: EQ tone 0 (190 Hz lowpass) + a.d.2 distortion on a saw:
  EQ first -> the distortion regenerates highs; EQ last -> highs gone.
2 effect vs delay: distortion + one echo (fb 0): effect first -> the echo
  is a scaled copy of the distorted note (same h3/h1); effect after the
  delay -> different h3/h1 in the echo window.
3 delay vs chorus: ping-pong delay (echo 1 = left only) + chorus: a chorus
  AFTER the delay leaks echo 1 into the right channel."""
import numpy as np

import s1

SR = 44100
K = 57
F = s1.key_hz(K)


def band(x, ch, a, b, lo, hi):
    s = np.asarray(x[ch][int(a * SR):int(b * SR)], float)
    S = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
    f = np.fft.rfftfreq(len(s), 1 / SR)
    return float(S[(f >= lo) & (f < hi)].sum())


def h(x, ch, a, b, n):
    return band(x, ch, a, b, n * F - 20, n * F + 20)


def rms(x, ch, a, b):
    return float(np.sqrt((np.asarray(x[ch][int(a * SR):int(b * SR)], float) ** 2).mean()))


FX = dict(p77=1, p78=1, p79=127, p80=127, p81=127)


def main():
    specs = [
        dict(native=s1.patch(p60=0, **FX), notes=((0, 0.5, K, 127),), length=0.5),
        dict(native=s1.patch(p60=0), notes=((0, 0.5, K, 127),), length=0.5),
        dict(native=s1.patch(p0=0, p65=1, p35=12, p36=0, p37=64, **FX), notes=((0, 0.2, K, 127),), length=1.0),
        dict(native=s1.patch(p65=1, p35=12, p36=0, p37=64, p82=2, p66=1, p64=2, p56=127, p52=64, p53=64),
             notes=((0, 0.2, K, 127),), length=1.0),
        dict(native=s1.patch(p65=1, p35=12, p36=0, p37=64, p82=2), notes=((0, 0.2, K, 127),), length=1.0),
    ]
    for ours in (False, True):
        o = [x for x, _ in s1.render_many(specs, ours=ours)]
        print("ours" if ours else "s1")
        hi = lambda x: band(x, 0, 0.1, 0.4, 4000, 15000) / band(x, 0, 0.1, 0.4, 100, 400)
        print(f"  1 EQ-LP + dist: HF/LF {hi(o[0]):.2e}   (EQ-LP alone {hi(o[1]):.2e})")
        x = o[2]
        r = lambda a, b: (h(x, 0, a, b, 3) / h(x, 0, a, b, 1)) ** 0.5
        print(f"  2 dist + delay (sine): h3/h1 note {r(0.05, 0.19):.3f}  echo {r(0.55, 0.69):.3f}  (echo/note level "
              f"{rms(x, 0, 0.55, 0.69) / rms(x, 0, 0.05, 0.19):.3f})")
        for name, y in (("chorus", o[3]), ("no chorus", o[4])):
            print(f"  3 PP delay + {name}: echo-1 window R/L {rms(y, 1, 0.55, 0.69) / max(1e-9, rms(y, 0, 0.55, 0.69)):.3f}"
                  f"  echo-2 window L/R {rms(y, 0, 1.05, 1.0) if False else rms(y, 0, 0.8, 0.99) / max(1e-9, rms(y, 1, 0.8, 0.99)):.3f}")


if __name__ == "__main__":
    main()
