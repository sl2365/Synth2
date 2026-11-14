#!/usr/bin/env python3
"""Render the Synth1 reference corpus into reference/renders/.

Groups:
  presets/  416 real-world patches, anonymized (patch0001..): their values
            are the jobs of manifest_presets.jsonl (reference zip), re-rendered
  sweeps/   each parameter swept over its native range in a context patch that
            makes it audible (DRY + section enablers)
  pitch/    every osc shape across the keyboard
  filter/   osc2 noise through every filter type x cutoff x resonance
  velocity/ velocity response with/without velocity sensitivity

Usage: corpus.py [group ...]   (default: all)
A manifest.jsonl next to the renders records the exact job of every file.
"""
import json
import sys
import time

import s1
import sy1

ROOT = s1.ROOT
OUT = ROOT / "reference/renders"

NOTE = ((0.0, 1.0, 60, 100),)
PRESET_NOTES = ((0.0, 1.0, 48, 100), (1.5, 2.5, 60, 100), (1.5, 2.5, 64, 100), (1.5, 2.5, 67, 100), (3.0, 3.5, 72, 50))


def safe(s):
    return "".join(c if c.isalnum() or c in "-_." else "_" for c in s)[:60]


def preset_jobs():
    for j in [json.loads(line) for line in open(OUT / "manifest_presets.jsonl")]:
        yield "presets", j["name"], {int(k): v for k, v in j["native"].items()}, j["notes"], j["length"], j["meta"]


# ---- parameter sweeps --------------------------------------------------------

OSC = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 45, 72, 76, 91, 95, 96, 97}
MODENV = {10, 11, 12, 13, 71}
FILTER = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}
AMP = {25, 26, 27, 28, 29, 30, 90}
ARP = {31, 32, 33, 34, 59}
DELAY = {35, 36, 37, 65, 82, 83, 98}
PLAY = {38, 39, 40, 74}
LFO1 = {41, 42, 43, 44, 57, 67, 68}
LFO2 = {46, 47, 48, 49, 58, 69, 70}
WHEEL = {50, 51}
CHORUS = {52, 53, 54, 55, 56, 64, 66}
EQ = {60, 61, 62, 63}
UNISON = {73, 75, 84, 85, 92, 93}
EFFECT = {77, 78, 79, 80, 81}
SKIP = {86, 87, 88, 89, 94}  # MIDI CC routing / polyphony: no sound of their own


def sweep_values(pid):
    lo, hi = sy1.RANGE[pid]
    if hi - lo <= 48:
        return list(range(lo, hi + 1))
    return sorted(set(range(lo, hi + 1, 4)) | {hi, 64})


def sweep_context(pid):
    """(patch, notes, length, extra job kwargs) making `pid` audible."""
    p = s1.patch()
    notes, length, kw = NOTE, 2.0, {}
    if pid in OSC:
        p[5] = 64  # both oscillators audible
        if pid in (6, 7, 45):
            p[1] = 1
            p[2] = 71
        if pid in (95, 96, 97):
            p[95] = 100
    elif pid in MODENV:
        p.update({10: 1, 11: 100, 12: 20, 13: 60, 5: 64, 1: 2})
    elif pid in FILTER:
        p.update({19: 50, 20: 60, 21: 100, 15: 0, 16: 60, 17: 30, 18: 50})
        notes, length = ((0.0, 1.0, 60, 100),), 2.5
    elif pid in AMP:
        p.update({25: 10, 26: 60, 27: 64, 28: 50})
        notes, length = ((0.0, 1.0, 60, 80),), 3.0
    elif pid in ARP:
        p.update({59: 1, 31: 2, 33: 9, 34: 64})
        notes, length = ((0.0, 2.0, 60, 100), (0.0, 2.0, 64, 100), (0.0, 2.0, 67, 100)), 2.5
    elif pid in DELAY:
        p.update({65: 1, 35: 8, 36: 64, 37: 64})
        notes, length = ((0.0, 0.1, 60, 100),), 3.0
    elif pid in PLAY:
        p.update({38: 1, 39: 60, 40: 12})
        notes, length = ((0.0, 0.6, 60, 100), (0.4, 1.2, 72, 100), (1.3, 1.8, 48, 100)), 2.5
        kw["bends"] = ((1.5, 16383),)
    elif pid in LFO1:
        p.update({57: 1, 41: 2, 42: 2, 43: 80, 44: 80, 19: 64, 20: 40})
    elif pid in LFO2:
        p.update({58: 1, 46: 2, 47: 2, 48: 80, 49: 80, 19: 64, 20: 40})
    elif pid in WHEEL:
        p.update({57: 1, 41: 2, 42: 2, 43: 60, 44: 40, 19: 64})
        kw["ccs"] = ((0.0, 1, 127),)
    elif pid in CHORUS:
        p.update({66: 1, 64: 2, 52: 64, 53: 64, 54: 64, 55: 64, 56: 100})
    elif pid in EQ:
        p.update({1: 4, 5: 127})
    elif pid in UNISON:
        p.update({73: 1, 75: 40, 84: 100, 93: 4})
    elif pid in EFFECT:
        p.update({77: 1, 78: 0, 79: 64, 80: 64, 81: 100})
    return p, notes, length, kw


def sweep_jobs():
    for pid in range(99):
        if pid in SKIP:
            continue
        types = range(10) if pid in (79, 80, 81) else [None]
        for etype in types:
            base, notes, length, kw = sweep_context(pid)
            if etype is not None:
                base[78] = etype
            for v in sweep_values(pid):
                p = dict(base)
                p[pid] = v
                tag = f"p{pid:02d}" + (f"_t{etype}" if etype is not None else "")
                yield "sweeps", f"{tag}/v{v:+04d}", p, notes, length, {"param": pid, "value": v, "effect_type": etype, **kw}


def pitch_jobs():
    keys = range(12, 121, 6)
    for shape in range(4):
        for k in keys:
            yield "pitch", f"osc1_s{shape}/k{k:03d}", s1.patch(p0=shape), ((0.0, 0.8, k, 100),), 1.0, {"key": k}
    for shape in range(1, 5):
        for k in keys:
            yield "pitch", f"osc2_s{shape}/k{k:03d}", s1.patch(p1=shape, p5=127), ((0.0, 0.8, k, 100),), 1.0, {"key": k}
    for shape in range(4):
        for octv in (0, 1):
            for k in keys:
                p = s1.patch(p95=127, p96=shape, p97=octv, p5=0)
                p[0] = 0
                yield "pitch", f"sub_s{shape}_o{octv}/k{k:03d}", p, ((0.0, 0.8, k, 100),), 1.0, {"key": k, "mix_with_sine": True}


def filter_jobs():
    for ftype in range(5):
        for res in (0, 32, 64, 96, 127):
            for cut in sorted(set(range(0, 128, 8)) | {127}):
                p = s1.patch(p1=4, p5=127, p14=ftype, p19=cut, p20=res, p21=64)
                yield "filter", f"t{ftype}_r{res:03d}/c{cut:03d}", p, ((0.0, 1.2, 60, 100),), 1.5, {"type": ftype, "res": res, "cut": cut}
            for sat in (32, 64, 127):
                p = s1.patch(p1=4, p5=127, p14=ftype, p19=64, p20=res, p21=64, p23=sat)
                yield "filter", f"t{ftype}_r{res:03d}/sat{sat:03d}", p, ((0.0, 1.2, 60, 100),), 1.5, {"type": ftype, "res": res, "sat": sat}


def velocity_jobs():
    for sens in (0, 32, 64, 96, 127):
        for vel in (1, 16, 32, 48, 64, 80, 96, 112, 127):
            yield "velocity", f"amp_s{sens:03d}/v{vel:03d}", s1.patch(p30=sens), ((0.0, 0.5, 60, vel),), 0.8, {"vel": vel}
    for sw in (0, 1):
        for vel in (1, 32, 64, 96, 127):
            p = s1.patch(p19=30, p21=110, p16=40, p17=0, p24=sw)
            yield "velocity", f"filter_sw{sw}/v{vel:03d}", p, ((0.0, 0.8, 60, vel),), 1.0, {"vel": vel}


GROUPS = {"presets": preset_jobs, "sweeps": sweep_jobs, "pitch": pitch_jobs, "filter": filter_jobs, "velocity": velocity_jobs}


def main(groups):
    OUT.mkdir(parents=True, exist_ok=True)
    for g in groups:
        t0 = time.time()
        jobs, manifest = [], []
        for group, name, native, notes, length, meta in GROUPS[g]():
            path = OUT / group / f"{name}.wav"
            path.parent.mkdir(parents=True, exist_ok=True)
            kw = {k: meta.pop(k) for k in ("ccs", "bends") if k in meta}
            jobs.append(s1.job_text(path, native, notes, length=length, **kw))
            manifest.append({"group": group, "name": name, "file": str(path.relative_to(OUT)), "native": native,
                             "notes": notes, "length": length, "sr": 44100, "tempo": 120, "preroll": 0.25,
                             **kw, "meta": meta})
        s1.render_batch("".join(jobs))
        with open(OUT / f"manifest_{g}.jsonl", "w") as f:
            for m in manifest:
                f.write(json.dumps(m) + "\n")
        print(f"{g}: {len(jobs)} renders in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main(sys.argv[1:] or list(GROUPS))
