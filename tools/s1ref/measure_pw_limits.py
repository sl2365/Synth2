"""Pulse width limits: the original's pulse duty never reaches 0 or 1.

osc1 pulse alone; the effective low-part duty is read from the fundamental's
amplitude (|h1| ∝ sin(pi d), calibrated on static pw 8 = 8 / 254), so it
folds d / 1 - d: values are in 1/254 units, <= 127.
- static pw 0 / 1 / 2 at several notes;
- mod env (dest pw, attack 0, decay 127 = env ~1 at the onset) at amount 0
  (pulls d to 0) and 127 (pushes d to 1) from several pw.

Result (2026-11-01): pw 0 = pw 1 = 1.25 at every note (pw 2 = 2.0); env
down from any pw = 1.25, env up = 1.375 → final duty clamped to
[1.25 / 254, 1 - 1.375 / 254] (Voice.hpp). Before: pw 0 (and full-depth
square LFO / env on pw) rendered SILENCE in ours.

Usage: measure_pw_limits.py  (→ reference/meta/pw_limits.json, ours alongside)
"""
import json

import numpy as np

import s1

SR = 44100


def duty(x, note, a8):
    f0 = 440 * 2 ** ((note - 69) / 12)
    s = x[0][int(0.005 * SR) : int(0.025 * SR)]
    X = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    a = X[np.argmin(abs(np.fft.rfftfreq(len(s), 1 / SR) - f0))]
    if a8 is None:
        return a
    return float(np.arcsin(min(1.0, a / a8 * np.sin(np.pi * 8 / 254))) / np.pi * 254)


def main():
    cases = {}
    for note in (36, 60, 84):
        for pw in (8, 0, 1, 2):
            cases[f"note{note} static pw{pw}"] = (note, {8: pw})
    for amt, tag in ((0, "env down"), (127, "env up")):
        for pw in (20, 64, 110, 127):
            cases[f"note72 {tag} pw{pw}"] = (72, {8: pw, 10: 1, 71: 2, 11: amt, 12: 0, 13: 127})
    cases["note72 static pw8"] = (72, {8: 8})
    specs = [{"native": {**s1.DRY, 0: 2, 5: 0, **v}, "notes": ((0.0, 1.0, note, 100),), "length": 1.0}
             for note, v in cases.values()]
    res = {}
    for who, outs in (("ref", s1.render_many(specs)), ("ours", s1.render_many(specs, ours=True))):
        cal = {}
        for (k, (note, v)), (x, _) in zip(cases.items(), outs):
            if v == {8: 8}:
                cal[note] = duty(x, note, None)
        for (k, (note, v)), (x, _) in zip(cases.items(), outs):
            res.setdefault(k, {})[who] = round(duty(x, note, cal[note]), 3)
    for k, v in res.items():
        print(f"{k:24s} ref {v['ref']:7.3f}  ours {v['ours']:7.3f}")
    path = s1.ROOT / "reference/meta/pw_limits.json"
    path.write_text(json.dumps(res, indent=1))
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
