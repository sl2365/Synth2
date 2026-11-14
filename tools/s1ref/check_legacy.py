"""Check convert_legacy (sy1.py, same law as Sy1Codec.hpp) against the
original on every old-version patch found: its raw values go through the
state chunk with the patch's version (no ver line = 100, measured on the
.sy1 loader) and the AU's values are compared on all 99 params.

--fixture also writes EngineCoreTests/legacy_fixture.txt (one patch per
line: `ver raw0..raw98 | expected0..expected98`) for the C++ test.

Usage: check_legacy.py [--fixture] [extra bank folder...]
(reference/presets is always searched; the committed fixture was made with
it plus a Synth1 bank folder.)"""
import sys
from pathlib import Path

import sy1
from measure_legacy import fromnative, pmap
from s1 import ROOT

BANKS = [ROOT / "reference/presets"] + [Path(a) for a in sys.argv[1:] if not a.startswith("--")]


def legacy_patches():
    seen, out = set(), []
    for root in BANKS:
        for f in sorted(root.rglob("*")):
            if f.suffix.lower() not in (".sy1", ".zip") or (f.suffix.lower() == ".sy1" and f.parent.suffix == ".zip"):
                continue
            for p in sy1.load_bank(f):
                key = (p.version, tuple(p.raw))
                if sy1.LEGACY_GROUP.get(p.version) and key not in seen:
                    seen.add(key)
                    out.append(p)
    return out


def random_patches(per_version=12, seed=1):
    """Random in-range values for every converting version (no real files
    exist for 100 / 106 / 107 / 108)."""
    import random
    rng = random.Random(seed)
    out = []
    for ver in (100, 105, 106, 107, 108):
        for k in range(per_version):
            p = sy1.Patch(name=f"random {ver}/{k}", source="", version=ver)
            p.raw = [rng.randint(*sy1.RANGE[i]) if not 86 <= i <= 89 else sy1.INIT[i] for i in range(99)]
            out.append(p)
    return out


def main():
    patches = legacy_patches() + random_patches()
    got = pmap(lambda p: fromnative(p.version or 100, dict(enumerate(p.raw))), patches)
    bad = 0
    for p, ref in zip(patches, got):
        ours = sy1.convert_legacy(p.raw, p.version)
        diff = {i: (ours[i], ref[i]) for i in range(99) if ours[i] != ref[i]}
        if diff:
            bad += 1
            print(f"MISMATCH {p.name} ver {p.version}: {diff}")
    print(f"{len(patches)} legacy patches, {bad} mismatches "
          f"(versions {sorted({p.version for p in patches})})")
    if "--fixture" in sys.argv:
        path = ROOT / "EngineCoreTests/legacy_fixture.txt"
        lines = [f"{p.version} " + " ".join(map(str, p.raw)) + " | " + " ".join(str(ref[i]) for i in range(99))
                 for p, ref in zip(patches, got)]
        path.write_text("\n".join(lines) + "\n")
        print(f"wrote {path} ({len(lines)} patches)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
