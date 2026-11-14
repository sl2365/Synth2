#!/usr/bin/env python3
"""Cutoff knob smoothing: jump the cutoff AU parameter mid-note (job line
`pt`) and read the effective cutoff back through the filter's static
response (sine above cutoff, LP12, res 0; amplitude -> knob via a static
sweep of the original). Synth1 (and ours, Synth.hpp CutoffRamp): a LINEAR
ramp to the new value over 50 ms whatever the step, time-based (same at
96 kHz).
  measure_cutoff_glide.py [sr]"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import s1
import wav

KEY = 95
JUMPS = [(40, 100), (40, 50), (100, 40), (40, 41)]
MS = [0, 2, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60]


def main():
    sr = int(sys.argv[1]) if len(sys.argv) > 1 else 44100
    f = s1.key_hz(KEY)
    base = s1.patch(p0=0, p14=0, p20=0)

    def patch(c):
        q = dict(base)
        q[19] = c
        return q

    cuts = list(range(20, 128))
    stat = s1.render_many([dict(native=patch(c), notes=((0.0, 1.5, KEY, 127),), length=0.6, sr=sr) for c in cuts])
    amp = np.array([s1.tone_amp(x, sr, f, start=0.3, dur=0.2) for x, _ in stat])

    def knob(x):
        s = np.asarray(x[0], float)
        z = s * np.exp(-2j * np.pi * f * np.arange(len(s)) / sr)
        w = int(0.001 * sr)
        return np.interp(2 * np.abs(np.convolve(z, np.ones(w) / w, "same")), amp, cuts)

    for label, ours in (("synth1", False), ("ours", True)):
        with tempfile.TemporaryDirectory() as d:
            texts = [s1.job_text(Path(d) / f"{i}.wav", patch(a), ((0.0, 1.5, KEY, 127),), length=1.0, sr=sr)
                     .replace("end\n", f"pt 0.5 19 {(b + 0.5) / 128}\nend\n") for i, (a, b) in enumerate(JUMPS)]
            if ours:
                subprocess.run([str(s1.ROOT / "build/s2render/s2render"), "-"], input="".join(texts).encode(), check=True,
                               capture_output=True)
            else:
                s1.render_batch("".join(texts))
            res = [wav.read(Path(d) / f"{i}.wav")[0] for i in range(len(JUMPS))]
        print(label, "(knob units; 127 = past the map)  ms:", " ".join("%5d" % m for m in MS))
        t0 = int(0.5 * sr)
        for (a, b), x in zip(JUMPS, res):
            k = knob(x)
            print(f"  {a:3d} -> {b:3d}:", " ".join("%5.1f" % k[t0 + int(m * sr / 1000)] for m in MS))


if __name__ == "__main__":
    main()
