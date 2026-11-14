#!/usr/bin/env python3
"""Unison mode vs the original: every unison knob and its interactions.

  check_unison.py [name-substring] [--all]

Each case renders through s1ref and s2render and prints the level of each
channel (dB, ours - ref), compare.py's spec / env on the mono mix, the
side/mid ratio difference (stereo width, dB) and, for deterministic phases
(osc phase > 0, no noise), the null residual (dB below the reference).
Cases over 1 dB of level / width or 3 dB of env are flagged (*); --all
prints every case.
"""
import sys

import numpy as np

import compare
import s1

def grid(notes):
    """The original applies MIDI events at the start of their 16-sample
    sub-block (host-block relative): snap times to it so nulls stay valid."""
    q = lambda t: round(t * 44100 / 16) * 16 / 44100
    return tuple((q(on), q(off), key, vel) for on, off, key, vel in notes)


NOTE = ((0.0, 1.0, 60, 127),)
CHORD = grid(((0.0, 1.2, 48, 127), (0.1, 1.2, 55, 127), (0.2, 1.2, 64, 127), (0.3, 1.2, 67, 127)))
LEGATO = grid(((0.0, 0.6, 60, 127), (0.4, 1.0, 67, 127), (0.9, 1.4, 64, 127)))
FALLBACK = grid(((0.0, 1.2, 60, 127), (0.3, 0.7, 67, 127)))
DETACHED = grid(((0.0, 0.4, 60, 127), (0.5, 0.9, 67, 127), (1.0, 1.4, 64, 127)))


def base(**kw):
    p = s1.patch(p73=1, p75=40, p84=100, p85=24, p93=4, p91=1, p92=0)
    p.update({int(k[1:]): v for k, v in kw.items()})
    return p


def cases():
    for n in range(2, 9):
        for pan in (0, 32, 64, 100, 127):
            yield f"voices{n}/pan{pan}", base(p93=n, p84=pan), NOTE
    for n in (2, 3, 8):
        for det in (0, 1, 10, 40, 64, 127):
            yield f"voices{n}/detune{det}", base(p93=n, p75=det), NOTE
    for n in (2, 3, 5, 8):
        for pitch in (0, 12, 23, 25, 31, 48):
            yield f"voices{n}/pitch{pitch}", base(p93=n, p85=pitch), NOTE
    for n in (2, 8):
        for ph in (0, 1, 64, 127):
            for uph in (0, 1, 32, 64, 127):
                yield f"voices{n}/oscphase{ph}/uniphase{uph}", base(p93=n, p91=ph, p92=uph, p75=0, p84=64), NOTE
    for ph in (0, 64):
        yield f"osc1detune40/oscphase{ph}", base(p76=40, p91=ph), NOTE
        yield f"osc2saw+7/oscphase{ph}", base(p5=64, p2=71, p91=ph), NOTE
        yield f"osc2pulse/oscphase{ph}", base(p5=64, p1=2, p91=ph), NOTE
        yield f"osc2noise/oscphase{ph}", base(p5=64, p1=4, p91=ph), NOTE
        yield f"sync/oscphase{ph}", base(p5=127, p6=1, p2=76, p91=ph), NOTE
        yield f"ring/oscphase{ph}", base(p5=127, p7=1, p2=71, p91=ph), NOTE
        yield f"fm/oscphase{ph}", base(p45=40, p2=71, p91=ph), NOTE
        yield f"sub/oscphase{ph}", base(p95=100, p96=3, p91=ph), NOTE
        yield f"sub+osc1detune/oscphase{ph}", base(p95=100, p76=40, p91=ph), NOTE
    for poly in (2, 4, 8, 16, 32):
        yield f"chord/poly{poly}", base(p94=poly), CHORD
    yield "chord/poly8/voices3", base(p94=8, p93=3), CHORD
    for mode in (1, 2):
        yield f"playmode{mode}", base(p38=mode), LEGATO
        yield f"playmode{mode}/fallback", base(p38=mode), FALLBACK
        yield f"playmode{mode}/release50", base(p38=mode, p28=50), LEGATO
        yield f"playmode{mode}/detached/release50", base(p38=mode, p28=50), DETACHED
        yield f"playmode{mode}/detached/porta60", base(p38=mode, p39=60, p28=50), DETACHED
        yield f"playmode{mode}/oscphase0", base(p38=mode, p91=0), LEGATO
        yield f"playmode{mode}/porta60", base(p38=mode, p39=60), LEGATO
    yield "poly/porta60", base(p39=60), LEGATO
    yield "lfo pan", base(p57=1, p41=7, p42=5, p43=80, p44=100), NOTE
    yield "lfo pitch", base(p57=1, p41=2, p42=5, p43=70, p44=20), NOTE
    yield "patch pan 20", base(p90=20), NOTE
    yield "lp24 res", base(p14=1, p19=60, p20=90), NOTE


def db(x):
    return 10 * np.log10((x ** 2).mean() + 1e-20)


def metrics(ref, us, sr, deterministic):
    lvl = [db(us[c]) - db(ref[c]) for c in (0, 1)]
    _, spec, env = compare.score(ref, us, sr)
    width = lambda x: db(x[0] - x[1]) - db(x[0] + x[1])
    wr, wu = width(ref), width(us)
    w = wu - wr if wr > -60 or wu > -60 else 0.0
    null = db(us - ref) - db(ref) if deterministic else None
    return lvl, spec, env, w, null


def main():
    sub = next((a for a in sys.argv[1:] if not a.startswith("--")), "")
    every = "--all" in sys.argv
    todo = [c for c in cases() if sub in c[0]]
    specs = [{"native": p, "notes": notes, "length": max(n[1] for n in notes) + 0.5} for _, p, notes in todo]
    refs, ours = s1.render_many(specs), s1.render_many(specs, ours=True)
    bad = 0
    for (name, p, _), (ref, sr), (us, _) in zip(todo, refs, ours):
        det = p[91] > 0 and not (p[5] > 0 and p[1] == 4)
        (ll, lr), spec, env, w, null = metrics(ref, us, sr, det)
        flag = max(abs(ll), abs(lr), abs(w)) > 1 or env > 3
        bad += flag
        if flag or every:
            print(f"{'*' if flag else ' '} {name:36s} L {ll:+6.2f} R {lr:+6.2f}  spec {spec:5.2f} env {env:5.2f}"
                  f"  width {w:+6.2f}" + (f"  null {null:6.1f}" if null is not None else ""))
    print(f"{len(todo)} cases, {bad} flagged")


if __name__ == "__main__":
    main()
