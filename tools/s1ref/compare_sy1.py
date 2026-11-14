#!/usr/bin/env python3
"""Compare Oxo Synth2 with the original on arbitrary .sy1 patches.

  compare_sy1.py <folder|file.sy1>... [--per-bank N] [--seed S] [--worst N] [--wav DIR]

Picks N random patches per bank folder (all of a file argument), renders
each through s1ref (the RAW file values for current versions, so the
original's own handling of out-of-range values is exercised) and s2render
(values as our codec parses them), same notes as the presets corpus, and
scores them like compare.py. --wav keeps both renders (<name>.s1.wav /
<name>.s2.wav) for listening.
"""
import random
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

import compare
import corpus
import s1
import sy1


def opt(name, default, cast=int):
    for a in sys.argv[1:]:
        if a.startswith(f"--{name}="):
            return cast(a.split("=", 1)[1])
    return default


def pick(paths, per_bank, rng):
    out = []
    for p in paths:
        if p.is_file():
            out.append(p)
            continue
        for bank in sorted({f.parent for f in p.rglob("*.sy1")}):
            files = sorted(bank.glob("*.sy1"))
            out += rng.sample(files, min(per_bank, len(files)))
    return out


def main():
    paths = [Path(a).expanduser() for a in sys.argv[1:] if not a.startswith("--")]
    rng = random.Random(opt("seed", 1))
    files = pick(paths, opt("per-bank", 1), rng)
    wav_dir = opt("wav", None, lambda s: Path(s).expanduser())
    patches = [sy1.parse(f.read_text(errors="replace"), str(f)) for f in files]
    spec = lambda values: {"native": dict(enumerate(values)), "notes": corpus.PRESET_NOTES, "length": 6.0}
    refs = s1.render_many([spec(p.values if p.legacy else p.raw) for p in patches])
    ours = s1.render_many([spec(p.values) for p in patches], ours=True)
    rows = []
    for f, p, (ref, sr), (us, _) in zip(files, patches, refs, ours):
        name = f"{f.parent.name}/{f.stem}"
        if np.abs(ref).max() < 1e-6 and np.abs(us).max() < 1e-6:
            rows.append((name, 0.0, 0.0, 0.0, bool(p.clamped)))
            continue
        rows.append((name, *compare.score(ref, us, sr), bool(p.clamped)))
        if wav_dir:
            base = wav_dir / compare_safe(name)
            base.parent.mkdir(parents=True, exist_ok=True)
            wavfile.write(base.with_suffix(".s1.wav"), sr, ref.T.astype(np.float32))
            wavfile.write(base.with_suffix(".s2.wav"), sr, us.T.astype(np.float32))
    rows.sort(key=lambda r: -(abs(r[1]) + r[2] + r[3]))
    lv, sp, en = (np.array([r[i] for r in rows]) for i in (1, 2, 3))
    print(f"{len(rows)} patches | level |mean| {np.abs(lv).mean():.2f} dB (max {np.abs(lv).max():.2f}) | "
          f"spec mean {sp.mean():.2f} dB (max {sp.max():.2f}) | env mean {en.mean():.2f} dB (max {en.max():.2f})")
    for q in (50, 90):
        print(f"  p{q}: level {np.percentile(np.abs(lv), q):.2f}  spec {np.percentile(sp, q):.2f}  "
              f"env {np.percentile(en, q):.2f}")
    for name, l, s, e, odd in rows[: opt("worst", 15)]:
        print(f"  {name[:56]:56s} level {l:+6.2f}  spec {s:5.2f}  env {e:5.2f}{'  (odd values)' if odd else ''}")


def compare_safe(name):
    return Path(*(corpus.safe(part) for part in Path(name).parts))


if __name__ == "__main__":
    main()
