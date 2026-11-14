#!/usr/bin/env python3
"""Synth1's master "vol" knob (not a parameter): chunk word 651 (default 64).
Gain of every value 0..127 relative to 64, on a steady sine (osc1 sine, key
69, vel 127, DRY patch) -> reference/meta/volume.json."""
import json

import numpy as np

import s1

VOLUME_WORD = 651


def main():
    base = dict(native=s1.patch(p0=0), notes=((0.0, 1.5, 69, 127),), length=1.0)
    res = s1.render_many([dict(base, raw={VOLUME_WORD: v}) for v in range(128)])
    amps = [s1.tone_amp(x, sr, 440.0) for x, sr in res]
    gains = [a / amps[64] for a in amps]
    out = s1.ROOT / "reference/meta/volume.json"
    out.write_text(json.dumps({"word": VOLUME_WORD, "default": 64, "gain": gains}, indent=1))
    for v in range(0, 128, 8):
        g = gains[v]
        print(v, f"{g:.6f}", f"{20 * np.log10(g):+.2f} dB" if g > 0 else "-inf")
    print(127, f"{gains[127]:.6f}", f"{20 * np.log10(gains[127]):+.2f} dB")


if __name__ == "__main__":
    main()
