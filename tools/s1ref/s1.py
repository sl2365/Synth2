"""Helpers to drive s1ref from Python: build jobs, render, load results."""
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

import wav

ROOT = Path(__file__).resolve().parents[2]
S1REF = ROOT / "build/s1ref/s1ref"

# A neutral patch for isolating one section: osc1 saw only, filter wide open,
# no envelopes on pitch, no LFOs, no effects, no unison, amp gate-like.
DRY = {
    0: 1, 1: 1, 2: 64, 3: 64, 4: 1, 5: 0, 6: 0, 7: 0, 8: 64, 9: 0, 10: 0, 11: 64, 12: 0, 13: 0,
    14: 1, 15: 0, 16: 0, 17: 127, 18: 0, 19: 127, 20: 0, 21: 64, 22: 0, 23: 0, 24: 0,
    25: 0, 26: 0, 27: 127, 28: 0, 29: 64, 30: 0, 31: 1, 32: 0, 33: 0, 34: 64, 35: 8, 36: 0, 37: 0,
    38: 0, 39: 0, 40: 2, 41: 1, 42: 0, 43: 64, 44: 0, 45: 0, 46: 1, 47: 0, 48: 64, 49: 0,
    50: 64, 51: 64, 57: 0, 58: 0, 59: 0, 60: 64, 61: 64, 62: 64, 63: 64, 64: 1, 65: 0, 66: 0,
    67: 0, 68: 0, 69: 0, 70: 0, 71: 0, 72: 64, 73: 0, 74: 0, 75: 0, 76: 0, 77: 0, 78: 0,
    82: 0, 84: 64, 85: 24, 90: 64, 91: 0, 92: 0, 93: 2, 94: 16, 95: 0, 96: 0, 97: 0,
}


def job_text(out, native=None, notes=(), length=2.0, sr=44100, tempo=120, preroll=0.25, ccs=(), bends=(), press=(), beat0=0.0,
             raw=None):
    lines = [f"job {out}", f"sr {sr}", f"len {length}", f"tempo {tempo}", f"preroll {preroll}", f"beat0 {beat0}"]
    for k, v in sorted((native or {}).items()):
        lines.append(f"n {k} {int(v)}")
    for k, v in sorted((raw or {}).items()):  # raw chunk words (s1ref only)
        lines.append(f"w {k} {int(v)}")
    for on, off, key, vel in notes:
        lines.append(f"note {on} {off} {key} {vel}")
    for t, num, val in ccs:
        lines.append(f"cc {t} {num} {val}")
    for t, val in bends:
        lines.append(f"bend {t} {val}")
    for t, val in press:
        lines.append(f"press {t} {val}")
    lines.append("end")
    return "\n".join(lines) + "\n"


def _run_one(text):
    subprocess.run([str(S1REF), "batch", "-"], input=text.encode(), check=True, capture_output=True)


def render_batch(text, workers=None):
    """Synth1 keeps process-wide state (oscillator phase / PRNG), so only the
    first instance in a process is reproducible: run every job in its own
    process, in parallel."""
    jobs, cur = [], []
    for line in text.splitlines(keepends=True):
        cur.append(line)
        if line.strip() == "end":
            jobs.append("".join(cur))
            cur = []
    with ThreadPoolExecutor(max_workers=workers or os.cpu_count()) as pool:
        list(pool.map(_run_one, jobs))


def render_many(specs, ours=False):
    """specs: list of dicts with job_text kwargs (native, notes, length, ...).
    Returns list of (stereo array, sr). ours=True renders through s2render."""
    with tempfile.TemporaryDirectory() as d:
        texts = []
        for i, spec in enumerate(specs):
            spec = dict(spec)
            native = spec.pop("native", None)
            notes = spec.pop("notes", ((0.0, 1.0, 60, 100),))
            texts.append(job_text(Path(d) / f"{i}.wav", native, notes, **spec))
        if ours:
            subprocess.run([str(ROOT / "build/s2render/s2render"), "-"], input="".join(texts).encode(), check=True,
                           capture_output=True)
        else:
            render_batch("".join(texts))
        return [wav.read(Path(d) / f"{i}.wav") for i in range(len(specs))]


def tone_amp(x, sr, freq, start=0.3, dur=0.5):
    """Exact amplitude of a steady sinusoid at `freq` (least squares over a
    window, mono = left channel)."""
    a = int(start * sr)
    s = np.asarray(x[0][a : a + int(dur * sr)], dtype=np.float64)
    t = np.arange(len(s)) / sr
    m = np.stack([np.cos(2 * np.pi * freq * t), np.sin(2 * np.pi * freq * t), np.ones_like(t)], 1)
    c, *_ = np.linalg.lstsq(m, s, rcond=None)
    return float(np.hypot(c[0], c[1]))


def harmonics(x, sr, f0, count, start=0.3, dur=0.5):
    """Amplitudes of harmonics 1..count of f0 (joint least squares)."""
    a = int(start * sr)
    s = np.asarray(x[0][a : a + int(dur * sr)], dtype=np.float64)
    t = np.arange(len(s)) / sr
    cols = [np.ones_like(t)]
    hs = [h for h in range(1, count + 1) if h * f0 < sr / 2]
    for h in hs:
        cols += [np.cos(2 * np.pi * h * f0 * t), np.sin(2 * np.pi * h * f0 * t)]
    c, *_ = np.linalg.lstsq(np.stack(cols, 1), s, rcond=None)
    amps = np.hypot(c[1::2], c[2::2])
    phases = np.arctan2(-c[2::2], c[1::2])
    return np.array(hs), amps, phases


def key_hz(key):
    return 440.0 * 2 ** ((key - 69) / 12)


def render(native=None, notes=((0.0, 1.0, 60, 100),), **kw):
    """Render one job to a temp file and return (stereo array, sr)."""
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "out.wav"
        render_batch(job_text(out, native, notes, **kw))
        return wav.read(out)


def patch(**overrides):
    p = dict(DRY)
    for k, v in overrides.items():
        p[int(k.lstrip("p"))] = v
    return p


def peak(x):
    return float(np.abs(x).max())
