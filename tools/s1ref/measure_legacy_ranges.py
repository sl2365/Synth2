"""What the original plays for OUT-OF-RANGE legacy values that its .sy1 file
loader keeps (the chunk loader clamps them): delay time 35 > 19 (old range
1..40) and lfo dest 0.

Method: a fresh Synth1 instance loads program 001 of bank 0 at creation, so
bankfolder0 (jp.daichi.synth1 prefs, backed up and restored) points at a
temp folder holding the test patch as 001.sy1, and the job sets no native
values. One render at a time (the folder is global).

- delay: pluck (saw, sustain 0) + ST delay, feedback 0, tempo 120; the echo
  lag = cross-correlation peak of the dry pluck with the output.
- lfo dest 0: lfo2 square at full depth renders bit-identical to depth 0
  (= no effect).

Result (2026-10-29): delay 8..19 = kDelayBeats, 20..40 = 4 beats (= 19);
lfo dest 0 = no effect.
SUPERSEDED (2026-11-01, measure_out_of_range.py): the chunk loader keeps
out-of-range values too, and lfo dest 0 plays as dest 1 (osc2 pitch, silent
in this osc1-only patch, hence the false "no effect")."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import s1
import sy1

PREFS = "jp.daichi.synth1"


def sy1_text(values, ver=105):
    return "test\ncolor=default\nver=%d\n" % ver + "".join(f"{i},{v}\n" for i, v in sorted(values.items()))


def base_patch():
    p = {i: sy1.INIT[i] for i in range(67)}
    p.update({k: v for k, v in s1.DRY.items() if k < 67})
    return p


def loaded(values, bank, notes, length, tempo=120):
    (bank / "001.sy1").write_text(sy1_text(values))
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "o.wav"
        s1._run_one(s1.job_text(out, None, notes, length=length, tempo=tempo))
        return s1.wav.read(out)


def echo_lag(x, sr):
    mono = x[0] + x[1]
    dry_len = int(0.15 * sr)
    dry = mono[:dry_len]
    c = np.correlate(mono, dry, mode="valid")
    start = int(0.2 * sr)
    k = start + int(np.argmax(np.abs(c[start:])))
    return k / sr, float(np.abs(c[k]) / np.abs(c[0]))


def main():
    backup = Path(tempfile.mkdtemp()) / "prefs.plist"
    subprocess.run(["defaults", "export", PREFS, str(backup)], check=True)
    bank = Path(tempfile.mkdtemp())
    test = backup.with_name("test.plist")
    shutil.copy(backup, test)
    subprocess.run(["/usr/libexec/PlistBuddy", "-c", f"Set :global:bankfolder0 {bank}", str(test)], check=True)
    subprocess.run(["defaults", "import", PREFS, str(test)], check=True)
    res = {"delay_lag_s": {}, "lfo_dest0": {}}
    try:
        for t in list(range(5, 41)):
            p = base_patch()
            p.update({25: 0, 26: 40, 27: 0, 28: 0, 65: 1, 36: 0, 37: 127, 35: t})
            x, sr = loaded(p, bank, ((0.0, 0.1, 60, 127),), 6.0)
            lag, rel = echo_lag(x, sr)
            res["delay_lag_s"][t] = [round(lag, 5), round(rel, 3)]
            print(f"delay {t}: lag {lag * 1000:.1f} ms ({lag / 0.5:.4f} beats) rel {rel:.2f}", file=sys.stderr)
        # lfo2 square at full depth: dest 0 vs the same patch at depth 0
        # (bit-identical = no effect), against real dests as a control
        outs = {}
        for dest, depth in ((0, 0), (0, 127), (2, 127), (4, 127)):
            p = base_patch()
            p.update({57: 0, 58: 1, 46: dest, 47: 2, 48: 40, 49: depth, 19: 70})
            outs[(dest, depth)] = loaded(p, bank, ((0.0, 2.0, 60, 100),), 2.0)[0][0]
        for (dest, depth), x in outs.items():
            d = float(np.sqrt(np.mean((x - outs[(0, 0)]) ** 2)))
            res["lfo_dest0"][f"dest {dest} depth {depth}"] = d
            print(f"lfo2 dest {dest} depth {depth}: rms diff vs depth 0 = {d:.2e}", file=sys.stderr)
    finally:
        subprocess.run(["defaults", "import", PREFS, str(backup)], check=True)
        shutil.rmtree(bank, ignore_errors=True)
    path = s1.ROOT / "reference/meta/legacy_ranges.json"
    path.write_text(json.dumps(res, indent=1))
    print(f"wrote {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
