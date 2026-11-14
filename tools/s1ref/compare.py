#!/usr/bin/env python3
"""Compare Oxo Synth2 against the reference corpus.

  compare.py <group> [name-substring] [--worst N] [--detail]

Renders the group's manifest jobs through build/s2render and scores each
pair:
  level   overall RMS difference (dB, ours - ref)
  spec    mean |third-octave band difference| over 40 Hz-16 kHz (dB),
          bands within 50 dB of the loudest only
  env     RMS difference of the 10 ms RMS envelope (dB, floor -60 dB rel peak)
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import s1
import wav

REF = s1.ROOT / "reference/renders"
BANDS = 40 * 2 ** (np.arange(0, 28) / 3)  # 40 Hz .. ~16 kHz


def band_db(x, sr):
    s = x.mean(0)
    n = len(s)
    if n < 1024:
        return None
    X = np.abs(np.fft.rfft(s * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    out = []
    for c in BANDS:
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(X[m].sum() + 1e-20))
    return np.array(out)


def env_db(x, sr, hop=0.01):
    s = x.mean(0)
    h = int(hop * sr)
    frames = len(s) // h
    e = np.sqrt((s[: frames * h].reshape(frames, h) ** 2).mean(1) + 1e-20)
    return 20 * np.log10(e)


def score(ref, ours, sr):
    rr = np.sqrt((ref**2).mean() + 1e-20)
    ro = np.sqrt((ours**2).mean() + 1e-20)
    level = 20 * np.log10(ro / rr)
    br, bo = band_db(ref, sr), band_db(ours, sr)
    keep = br > br.max() - 50
    spec = float(np.abs(bo[keep] - br[keep]).mean())
    er, eo = env_db(ref, sr), env_db(ours, sr)
    floor = er.max() - 60
    er, eo = np.maximum(er, floor), np.maximum(eo, floor)
    env = float(np.sqrt(((eo - er) ** 2).mean()))
    return level, spec, env


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    worst = 15
    for a in sys.argv[1:]:
        if a.startswith("--worst="):
            worst = int(a.split("=")[1])
    group = args[0]
    sub = args[1] if len(args) > 1 else ""
    jobs = [json.loads(l) for l in open(REF / f"manifest_{group}.jsonl")]
    jobs = [j for j in jobs if sub in j["name"]]
    with tempfile.TemporaryDirectory() as d:
        texts = []
        for i, j in enumerate(jobs):
            texts.append(s1.job_text(Path(d) / f"{i}.wav", {int(k): v for k, v in j["native"].items()}, j["notes"],
                                     length=j["length"], sr=j["sr"], tempo=j["tempo"], preroll=j["preroll"],
                                     ccs=j.get("ccs", ()), bends=j.get("bends", ())))
        subprocess.run([str(s1.ROOT / "build/s2render/s2render"), "-"], input="".join(texts).encode(), check=True,
                       capture_output=True)
        rows = []
        for i, j in enumerate(jobs):
            ref, sr = wav.read(REF / j["file"])
            ours, _ = wav.read(Path(d) / f"{i}.wav")
            if np.abs(ref).max() < 1e-6 and np.abs(ours).max() < 1e-6:
                rows.append((j["name"], 0.0, 0.0, 0.0))
                continue
            rows.append((j["name"], *score(ref, ours, sr)))
    rows.sort(key=lambda r: -(abs(r[1]) + r[2] + r[3]))
    lv = np.array([r[1] for r in rows])
    sp = np.array([r[2] for r in rows])
    en = np.array([r[3] for r in rows])
    print(f"{group}{'/' + sub if sub else ''}: {len(rows)} renders | level |mean| {np.abs(lv).mean():.2f} dB "
          f"(max {np.abs(lv).max():.2f}) | spec mean {sp.mean():.2f} dB (max {sp.max():.2f}) | "
          f"env mean {en.mean():.2f} dB (max {en.max():.2f})")
    for name, l, s, e in rows[:worst]:
        print(f"  {name:40s} level {l:+6.2f}  spec {s:5.2f}  env {e:5.2f}")


if __name__ == "__main__":
    main()
