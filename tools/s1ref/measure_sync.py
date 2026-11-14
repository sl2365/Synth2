#!/usr/bin/env python3
"""Hard sync: aliased (non-harmonic) energy of a synced osc2 saw, original vs
ours. Equal levels = same (naive, phase-0 reset) sync."""
import sys
import numpy as np, s1
SR = 44100
def alias_ratio(x, f0):
    s = np.asarray(x[0][int(0.2*SR):int(1.0*SR)], float); S = np.abs(np.fft.rfft(s * np.blackman(len(s)))) ** 2; f = np.fft.rfftfreq(len(s), 1/SR)
    h = np.zeros_like(f, bool)
    for k in range(1, int(SR / 2 / f0) + 1): h |= np.abs(f - k * f0) < 6
    return 10 * np.log10(S[~h].sum() / S[h].sum())
for key, p2 in ((72, 76), (84, 90), (60, 100)):
    specs = [dict(native=s1.patch(p0=1, p1=1, p5=127, p6=1, p2=p2), notes=((0, 1.1, key, 127),), length=1.1)]
    for ours in (False, True):
        x = s1.render_many(specs, ours=ours)[0][0]
        print(f"key {key} osc2 pitch {p2}: {'ours' if ours else 's1  '} alias/harmonic energy {alias_ratio(x, s1.key_hz(key)):+.1f} dB")
