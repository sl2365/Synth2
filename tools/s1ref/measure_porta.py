#!/usr/bin/env python3
"""Portamento law: glide time per knob.

  measure_porta.py   -> reference/meta/porta.json

Legato glides of +12 semitones (sine, the first note settled for 5 s),
pitch tracked at 0.5 ms. The original's glide (all fitted per knob, rms
~5 cents over the whole glide):
- every 16 samples r <- max(r * a - E * (1 - a), 0), r = distance to the
  target in cents, a = exp(-16 / (tau * sr)): an exponential approach
  aimed E PAST the target, so it lands in finite time; E = 1.35 % of the
  distance at the glide's start (the curves normalized by the interval
  coincide from 6 to 60 semitones; 18.75 cents here at 12, fitted with
  it); tau in seconds (same at 44.1 / 48 / 96 kHz);
- the pitch played is the target + ceil(r / 9.375) * 9.375 cents (1/128
  octave steps: the end of a slow glide is an audible staircase).
Glide sources (check_unison.py porta cases): a voice glides from ITS OWN
last pitch (never used: MIDI note 0, e.g. the first note after loading);
mono / legato always use voice slot 0 (unison: slots 0..N-1); auto mode
glides only legato notes (mono), never in poly.
"""
import json

import numpy as np
from scipy.optimize import minimize_scalar

import instfreq
import s1

SR = 44100
STEP = 1200 / 128
OVERSHOOT = 2 * STEP
VALS = [1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96,
        100, 104, 108, 112, 116, 120, 124, 127]


def simulate(tau, n):
    a = np.exp(-16 / (tau * SR))
    r, x = [], 1200.0
    for _ in range(n // 16 + 2):
        r.append(x)
        x = max(x * a - OVERSHOOT * (1 - a), 0.0)
    r = np.array(r)[np.arange(n) // 16]
    return STEP * np.ceil(r / STEP - 1e-9)


def main():
    notes = ((0.0, 9.0, 60, 127), (5.0, 9.0, 72, 127))  # 5 s = 220500 samples, on the 16-sample grid
    specs = [{"native": s1.patch(p0=0, p91=1, p38=2, p39=v), "notes": notes, "length": 8.0} for v in VALS]
    taus = []
    for v, (x, _) in zip(VALS, s1.render_many(specs)):
        c = instfreq.inst_cents(x[:1], SR, 261.6256, smooth=0.0005)
        m = np.abs(c[int(5.0 * SR):int(7.9 * SR)] - 1200)
        land = int(np.argmax(m < 1))
        res = minimize_scalar(lambda lt: np.mean((simulate(np.exp(lt), land) - m[:land]) ** 2),
                              bounds=(np.log(1e-4), np.log(5)), method="bounded")
        taus.append(round(float(np.exp(res.x)), 6))
        print(f"porta {v:3d}: tau {taus[-1] * 1000:8.3f} ms  rms {np.sqrt(res.fun):5.2f} ct  lands {land / SR * 1000:.0f} ms")
    out = {"vals": VALS, "tau": taus, "overshoot_cents": OVERSHOOT, "step_cents": STEP}
    (s1.ROOT / "reference/meta/porta.json").write_text(json.dumps(out, indent=1) + "\n")


if __name__ == "__main__":
    main()
