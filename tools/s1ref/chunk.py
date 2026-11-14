#!/usr/bin/env python3
"""Decode the 'Synth1 VST Chunk Data' blob from s1ref classinfo output."""
import plistlib
import struct
import subprocess
import sys
from pathlib import Path

S1REF = Path(__file__).resolve().parents[2] / "build/s1ref/s1ref"


def chunk(assignments=()):
    xml = subprocess.run([str(S1REF), "classinfo", *assignments], check=True, capture_output=True).stdout
    return plistlib.loads(xml)["data"]


def ints(blob):
    return struct.unpack("<%di" % (len(blob) // 4), blob[: len(blob) // 4 * 4])


if __name__ == "__main__":
    blob = chunk(sys.argv[1:])
    print(len(blob), blob[:32])
    words = ints(blob)
    for i, w in enumerate(words):
        if w:
            print(i, i * 4, w, hex(w & 0xFFFFFFFF))
