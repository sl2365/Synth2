"""Synth1 .sy1 patch parser (plain files, folders and zipped banks).

Format: line 1 = name, then `key=value` header lines (color, ver) and
`index,value` lines holding native integer values (see reference/meta/
nativemap.txt for each index's range). ver=113 is the native format of the
1.13 AU; 109..112 are identical. No ver line (= 100) and 100 / 105..108 are
converted like the original does (measure_legacy.py, convert_legacy);
`Patch.raw` keeps the values as read.
"""
import io
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

# Native values of the 1.13 AU's built-in init patch (fresh instance).
INIT = [int(v) for v in """
2 1 51 81 1 69 0 0 64 0 0 64 0 0 1 0 40 21 69 32 22 83 64 0 0 0 64 107 78 68 0 1 0 11 64 8 51 20 0 0
12 3 5 48 19 0 5 1 64 64 74 74 64 64 50 64 40 1 1 0 64 64 64 64 2 0 0 0 0 0 0 0 64 0 0 22 0 0 0 64
64 64 0 66 64 24 45057 44 45057 43 64 0 0 2 16 0 1 1 64""".split()]

# Valid native range per index (inclusive), from the AU's published steps.
RANGE = {i: (0, 127) for i in range(99)}
RANGE.update({
    0: (0, 3), 1: (1, 4), 4: (0, 1), 6: (0, 1), 7: (0, 1), 9: (-24, 24), 10: (0, 1), 14: (0, 4), 24: (0, 1),
    31: (1, 4), 32: (0, 3), 33: (0, 18), 35: (0, 19), 38: (0, 2), 40: (0, 24), 41: (1, 7), 42: (0, 5),
    46: (1, 7), 47: (0, 5), 57: (0, 1), 58: (0, 1), 59: (0, 1), 64: (1, 4), 65: (0, 1), 66: (0, 1),
    67: (0, 1), 68: (0, 1), 69: (0, 1), 70: (0, 1), 71: (0, 2), 73: (0, 1), 74: (0, 1), 77: (0, 1),
    78: (0, 9), 82: (0, 2), 85: (0, 48), 86: (0, 65535), 87: (0, 65535), 88: (0, 65535), 89: (0, 65535),
    93: (2, 8), 94: (1, 32), 96: (0, 3), 97: (0, 1),
})


@dataclass
class Patch:
    name: str
    source: str
    version: int
    values: list = field(default_factory=lambda: list(INIT))
    present: set = field(default_factory=set)
    clamped: dict = field(default_factory=dict)
    raw: list = field(default_factory=list)

    @property
    def legacy(self):
        return self.version < 112


def parse(text, source="", convert=True):
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    p = Patch(name=lines[0].strip(), source=source, version=0)
    for line in lines[1:]:
        line = line.strip()
        if line.startswith("ver="):
            p.version = int(line[4:] or 0)
        elif "," in line and "=" not in line:
            try:
                i, v = (int(x) for x in line.split(","))
            except ValueError:
                continue
            if 0 <= i < 99:
                lo, hi = RANGE[i]
                if not lo <= v <= hi:
                    p.clamped[i] = v
                    v = min(max(v, lo), hi)
                p.values[i] = v
                p.present.add(i)
    p.raw = list(p.values)
    if convert:
        p.values = convert_legacy(p.values, p.version)
    return p


LEGACY_GROUP = {0: "100", 100: "100", 105: "105", 106: "105", 107: "107", 108: "107"}
_legacy = None


def convert_legacy(values, version):
    """The original's conversion of an old-version patch (same law as
    EngineCore/Sy1Codec.hpp ConvertLegacy; data: reference/meta/legacy.json)."""
    global _legacy
    group = LEGACY_GROUP.get(version)
    if group is None:
        return list(values)
    if _legacy is None:
        import json
        _legacy = json.loads((Path(__file__).resolve().parents[2] / "reference/meta/legacy.json").read_text())
    old, new = list(values), list(values)
    for p, t in _legacy["versions"][group].items():
        p = int(p)
        new[p] = t[0] if len(set(t)) == 1 else t[min(max(old[p], 0), 127) - RANGE[p][0]]
    new[86:90] = [45057, 44, 45057, 43]
    for sens, target in ((50, 44), (51, 43)):
        new[sens] = 64 + old[sens] * (127 - old[target]) * 63 // (127 * 127)
    mono = old[38] != 0
    new[93] = 4 if mono else 2
    if group == "107":
        new[75] = _legacy["play_mode"]["107"]["75"]["1" if mono else "0"][min(max(old[75], 0), 127)]
    else:
        new[75] = 70 if mono else 48
    return new


def load_bank(path):
    """Yield Patch objects from a .sy1 file, a folder of them, or a zip bank."""
    path = Path(path)
    if path.is_dir():
        for f in sorted(path.iterdir()):
            if f.suffix.lower() == ".sy1":
                yield parse(f.read_text(encoding="latin-1"), str(f))
    elif path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as z:
            for n in sorted(z.namelist()):
                if n.lower().endswith(".sy1") and not n.startswith("__MACOSX"):
                    yield parse(io.TextIOWrapper(z.open(n), encoding="latin-1").read(), f"{path}:{n}")
    elif path.suffix.lower() == ".sy1":
        yield parse(path.read_text(encoding="latin-1"), str(path))
