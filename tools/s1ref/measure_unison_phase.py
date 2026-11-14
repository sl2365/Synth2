#!/usr/bin/env python3
"""Start phases of the osc1 detune copies inside a unison stack.

  measure_unison_phase.py

2 stacked sine voices at +-5.1 cents (unison detune 32), osc1 detune 127
(copies at +-20, 60, 100, 140 cents): all 18 partials are distinct, a least
squares fit over 3 s gives each one's start phase. Two notes (the 2nd after
a released 1st), osc phase 0 (free) and 64. Prints, per copy, the phase of
voice 1 minus voice 0 (main = osc1 itself), then the level of a 4-voice
stack over a single voice.

Result (original): osc phase 0 = the mains differ by their voice slots'
initial phases (measure_slot_phase.py; fresh slots 0..7 = kPhaseDeg) and
the copies are unrelated per voice (the stack sums by power, not
coherently); osc phase > 0 = everything coherent. The
original's uniform ~17 degree excess on every partial (both modes) is a fit
bias common to the whole voice (onset latency or a 0.06 cent pitch gap),
not a phase offset.
"""
import numpy as np

import s1

SR, KEY = 44100, 81
F0 = 440 * 2 ** ((KEY - 69) / 12)
D, d = 5.09714, 20.0  # kUnisonDetuneCents[32], osc1 detune 127
OFFS = [0, -7, -5, -3, -1, 1, 3, 5, 7]


def partials():
    return [(j, o, F0 * 2 ** (((2 * j - 1) * D + o * d) / 1200)) for j in (0, 1) for o in OFFS]


def phases(x):
    t = np.arange(int(0.05 * SR), int(2.95 * SR)) / SR
    m = np.concatenate([[np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)] for _, _, f in partials()]).T
    c = np.linalg.lstsq(m, x[: len(t) + int(0.05 * SR)][int(0.05 * SR):], rcond=None)[0]
    return np.degrees(np.arctan2(-c[1::2], c[0::2]))


def rms_db(x):
    return 20 * np.log10(np.sqrt((x ** 2).mean()))


def main():
    for ph in (0, 64):
        patch = s1.patch(p0=0, p73=1, p93=2, p75=32, p84=64, p76=127, p91=ph)
        specs = [{"native": patch, "notes": ((0.0, 3.0, KEY, 127),), "length": 3.0},
                 {"native": patch, "notes": ((0.0, 0.5, 60, 127), (1.0, 4.0, KEY, 127)), "length": 4.0}]
        for ours in (False, True):
            first, second = (r[0][0] for r in s1.render_many(specs, ours=ours))
            for name, seg in (("note 1", first), ("note 2", second[SR:])):
                p = phases(seg)
                diff = (p[9:] - p[:9]) % 360
                print(f"osc phase {ph:2d} {'s2' if ours else 's1'} {name}: v1 - v0 main {diff[0]:4.0f} | copies "
                      + " ".join(f"{v:4.0f}" for v in diff[1:]))
    spec = lambda uni: {"native": s1.patch(p73=uni, p93=4, p75=10, p84=64, p76=40), "notes": ((0.0, 2.0, 60, 127),),
                        "length": 2.0}
    for ours in (False, True):
        one, four = (r[0][0] for r in s1.render_many([spec(0), spec(1)], ours=ours))
        print(f"{'s2' if ours else 's1'} 4-voice stack (osc1 detune 40, phase 0) over one voice: "
              f"{rms_db(four) - rms_db(one):+.1f} dB (power sum +6.0, coherent +12.0)")


if __name__ == "__main__":
    main()
