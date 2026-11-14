"""Legacy patch conversion of the original: write native values through the
state chunk with version word 140 = N and read back what the AU keeps.
Only N = 100, 105, 106, 107, 108 convert (probed 0..120, 200, 1000).

Stage 1 (scan): every param 0..85 / 90..98 set to v (clamped to its range)
in one process, for every v in each param's range, per version.
Stage 2 (isolated): converted params probed alone over their full range in
the default context; a param whose isolated table differs from the scan
depends on other params (reported, studied separately).

Writes reference/meta/legacy.json:
  {"versions": {N: {param: [out for in = lo..hi]}}, "context": {...}}"""
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import sy1
from s1 import ROOT, S1REF

VERSIONS = (100, 105, 106, 107, 108)
IDS = [i for i in range(99) if not 86 <= i <= 89]


def fromnative(ver, assign):
    args = [str(S1REF), "fromnative", f"ver={ver}"] + [f"{k}={v}" for k, v in assign.items()]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        m = re.match(r"(\d+) \S+ (-?\d+)$", line)
        if m:
            res[int(m[1])] = int(m[2])
    return res


def pmap(fn, items):
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        return list(pool.map(fn, items))


def clamp(i, v):
    lo, hi = sy1.RANGE[i]
    return min(max(v, lo), hi)


def scan(ver):
    vals = list(range(-24, 128))
    rows = pmap(lambda v: fromnative(ver, {i: clamp(i, v) for i in IDS}), vals)
    table = {}
    for i in IDS:
        lo, hi = sy1.RANGE[i]
        out = [rows[vals.index(v)][i] for v in range(lo, hi + 1)]
        if out != list(range(lo, hi + 1)):
            table[i] = out
    return table


def isolated(ver, i):
    lo, hi = sy1.RANGE[i]
    rows = pmap(lambda v: fromnative(ver, {i: v}), range(lo, hi + 1))
    return [r[i] for r in rows]


def by_play_mode(ver, i):
    """75 / 93 depend on play mode 38 (the only dependency found)."""
    lo, hi = sy1.RANGE[i]
    out = {}
    for mode in (0, 1, 2):
        rows = pmap(lambda v: fromnative(ver, {i: v, 38: mode}), range(lo, hi + 1))
        out[mode] = [r[i] for r in rows]
    return out


def wheel_grid(ver):
    """50 / 51 (old wheel -> lfo1 depth / speed sens) depend on the lfo1
    depth 44 / speed 43 they modulate: out[b][s] for 44 = 43 = b."""
    bases = list(range(0, 128, 8)) + [127]
    jobs = [(b, s) for b in bases for s in range(128)]
    rows = pmap(lambda bs: fromnative(ver, {50: bs[1], 51: bs[1], 44: bs[0], 43: bs[0]}), jobs)
    g50, g51 = {}, {}
    for (b, s), r in zip(jobs, rows):
        g50.setdefault(b, []).append(r[50])
        g51.setdefault(b, []).append(r[51])
    return {"50": g50, "51": g51}


def main():
    path = ROOT / "reference/meta/legacy.json"
    if "--context-only" in sys.argv:
        data = json.loads(path.read_text())
        # 100 / 106 and 108 scan identical to 105 / 107 for these params
        data["play_mode"] = {ver: {i: by_play_mode(ver, i) for i in (75, 93)} for ver in (105, 107)}
        data["wheel"] = {ver: wheel_grid(ver) for ver in (105, 107)}
        path.write_text(json.dumps(data, indent=1))
        print(f"wrote {path}", file=sys.stderr)
        return
    result, context = {}, {}
    for ver in VERSIONS:
        table = scan(ver)
        for i in sorted(table):
            iso = isolated(ver, i)
            if iso != table[i]:
                context.setdefault(ver, {})[i] = {"scan": table[i], "isolated": iso}
                print(f"ver {ver} param {i}: CONTEXT DEPENDENT", file=sys.stderr)
            table[i] = iso
        result[ver] = table
        print(f"ver {ver}: converts {sorted(table)}", file=sys.stderr)
    path.write_text(json.dumps({"versions": result, "context": context}, indent=1))
    print(f"wrote {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
