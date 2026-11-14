"""What the original PLAYS for out-of-range native values found in real
patches (chunk / .sy1 values the original keeps, e.g. Unify-wrapped VST
states): each odd value is rendered through the chunk next to every in-range
candidate of the same param (+ the section switched off), in a patch where
that param is audible. A bit-identical candidate = the original's meaning.

Usage: measure_out_of_range.py [--sy1 VER] [case ...]
  (→ reference/meta/out_of_range.json; --sy1 = through the .sy1 file loader
  at that version instead of the chunk, key "<case>@sy1_<VER>")
"""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import s1
import sy1

PREFS = "jp.daichi.synth1"

SAW2 = {5: 127, 1: 1}  # osc2 saw only
PULSES = {0: 2, 1: 2, 5: 64, 8: 30, 45: 20}  # both oscs pulse + FM: every mod dest audible
CHORD = ((0.0, 1.6, 60, 100), (0.0, 1.6, 64, 100), (0.0, 1.6, 67, 100))
HELD = ((0.0, 4.0, 60, 100), (0.0, 4.0, 64, 100), (0.0, 4.0, 67, 100))

# name: (param, odd values, candidates, overrides, notes, length, extra candidate patches)
CASES = {
    "osc2_pitch": (2, [128, 135], list(range(100, 128)), SAW2, ((0.0, 0.6, 48, 100),), 0.8, {}),
    "osc2_kbd_track": (4, [2, 3], [0, 1], SAW2, ((0.0, 0.6, 72, 100),), 0.8, {}),
    "filter_type": (14, [5, 6], [0, 1, 2, 3, 4], {19: 60, 20: 100}, ((0.0, 0.6, 48, 100),), 0.8, {}),
    "filter_amount": (21, [128], [124, 125, 126, 127], {19: 0, 20: 60, 15: 0, 16: 0, 17: 127},
                      ((0.0, 0.8, 48, 100),), 1.0, {}),
    "arp_type": (31, [0, 5], [1, 2, 3, 4], {59: 1, 33: 11, 32: 1, 28: 20}, CHORD, 2.0, {"off": {59: 0}}),
    "arp_oct_range": (32, [4], [0, 1, 2, 3], {59: 1, 31: 1, 33: 15, 28: 20}, HELD, 4.0, {}),
    "arp_beat": (33, [19, 24, 35, 42, 64], list(range(0, 19)), {59: 1, 31: 1, 28: 20}, CHORD, 2.0, {}),
    "delay_time": (35, [25, 27, 28, 29, 40], list(range(14, 20)),
                   {65: 1, 36: 0, 37: 127, 25: 0, 26: 40, 27: 0, 28: 0}, ((0.0, 0.1, 60, 127),), 6.0, {}),
    "lfo1_dest": (41, [0], list(range(1, 8)), {**PULSES, 57: 1, 42: 2, 43: 40, 44: 127, 19: 70},
                  ((0.0, 1.5, 60, 100),), 1.5, {"off": {57: 0}}),
    "lfo2_dest": (46, [0], list(range(1, 8)), {**PULSES, 58: 1, 47: 2, 48: 40, 49: 127, 19: 70},
                  ((0.0, 1.5, 60, 100),), 1.5, {"off": {58: 0}}),
    "lfo1_on": (57, [2, 3], [0, 1], {41: 3, 42: 2, 43: 40, 44: 127, 19: 70}, ((0.0, 1.5, 60, 100),), 1.5, {}),
    "chorus_type": (64, [0, 5], [1, 2, 3, 4], {66: 1, 56: 127, 53: 100}, ((0.0, 1.0, 60, 100),), 1.5,
                    {"off": {66: 0}}),
    "mod_env_dest": (71, [3, 4], [0, 1, 2], {**PULSES, 10: 1, 11: 127, 13: 60}, ((0.0, 0.8, 48, 100),), 1.0,
                     {"off": {10: 0}}),
    "unison_pitch": (85, [49, 60, 85], list(range(0, 49)), {73: 1, 93: 2, 75: 0, 84: 64}, ((0.0, 0.6, 48, 100),),
                     0.8, {}),
}


def render_sy1(specs, ver):
    """Through the .sy1 FILE loader instead of the chunk: a fresh instance
    loads 001.sy1 of bankfolder0 (prefs backed up / restored), serial."""
    backup = Path(tempfile.mkdtemp()) / "prefs.plist"
    subprocess.run(["defaults", "export", PREFS, str(backup)], check=True)
    bank = Path(tempfile.mkdtemp())
    test = backup.with_name("test.plist")
    shutil.copy(backup, test)
    subprocess.run(["/usr/libexec/PlistBuddy", "-c", f"Set :global:bankfolder0 {bank}", str(test)], check=True)
    subprocess.run(["defaults", "import", PREFS, str(test)], check=True)
    outs = []
    try:
        for spec in specs:
            values = {**dict(enumerate(sy1.INIT)), **spec["native"]}
            (bank / "001.sy1").write_text(f"test\ncolor=default\nver={ver}\n" +
                                          "".join(f"{i},{v}\n" for i, v in sorted(values.items())))
            with tempfile.TemporaryDirectory() as d:
                out = Path(d) / "o.wav"
                s1._run_one(s1.job_text(out, None, spec["notes"], length=spec["length"]))
                outs.append(s1.wav.read(out))
    finally:
        subprocess.run(["defaults", "import", PREFS, str(backup)], check=True)
        shutil.rmtree(bank, ignore_errors=True)
    return outs


def measure(name, sy1_ver=None):
    param, odds, cands, over, notes, length, extra = CASES[name]
    labels, specs = [], []
    for v in odds + cands:
        labels.append(v)
        specs.append({"native": {**s1.DRY, **over, param: v}, "notes": notes, "length": length})
    for k, ov in extra.items():
        labels.append(k)
        specs.append({"native": {**s1.DRY, **over, **ov, param: cands[0]}, "notes": notes, "length": length})
    outs = [x for x, _ in (render_sy1(specs, sy1_ver) if sy1_ver else s1.render_many(specs))]
    ref = {lab: x for lab, x in zip(labels, outs)}
    res = {}
    for v in odds:
        x = ref[v]
        scale = float(np.sqrt(np.mean(x**2))) or 1.0
        diffs = {str(lab): float(np.sqrt(np.mean((x - y) ** 2))) / scale for lab, y in ref.items() if lab not in odds}
        best = min(diffs, key=diffs.get)
        res[v] = {"best": best, "diff": diffs[best], "rms": scale, "diffs": diffs}
        print(f"{name} {v}: best {best} (rel diff {diffs[best]:.2e}, rms {scale:.3g})", file=sys.stderr)
    return res


def main():
    args = sys.argv[1:]
    sy1_ver = int(args.pop(args.index("--sy1") + 1)) if "--sy1" in args else None
    names = [a for a in args if a != "--sy1"] or list(CASES)
    path = s1.ROOT / "reference/meta/out_of_range.json"
    out = json.loads(path.read_text()) if path.exists() else {}
    for n in names:
        out[f"{n}@sy1_{sy1_ver}" if sy1_ver else n] = measure(n, sy1_ver)
    path.write_text(json.dumps(out, indent=1))
    print(f"wrote {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
