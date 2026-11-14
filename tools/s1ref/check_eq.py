#!/usr/bin/env python3
"""EQ (60..63) vs the original: complete transfer functions (identical
noise) over a grid of tone, freq, level, Q and tone + peak; prints the rms
and max magnitude difference (dB, < 16 kHz) per case, flags > 1 dB.

  check_eq.py
"""
import numpy as np

import s1
from measure_eq import noise_patch, tf, FREQS, LEN
cases=[]
for t in (0,8,16,24,32,40,48,56,60,63,65,68,72,80,88,96,104,112,120,127): cases.append(("tone",dict(p60=t)))
for f in (0,32,64,96,104,112,120,127):
    for lv in (0,32,96,127):
        for q in (0,64,127): cases.append(("peak",dict(p61=f,p62=lv,p63=q)))
for t in (20,100):
    for f in (40,100): cases.append(("tone+peak",dict(p60=t,p61=f,p62=110,p63=64)))
specs=[dict(native=noise_patch(),notes=((0,LEN,60,127),),length=LEN)]+[dict(native=noise_patch(**kw),notes=((0,LEN,60,127),),length=LEN) for _,kw in cases]
A=s1.render_many(specs); B=s1.render_many(specs,ours=True)
xa=A[0][0][0].astype(float); xb=B[0][0][0].astype(float)
res=[]
for (kind,kw),(a,_),(b,_) in zip(cases,A[1:],B[1:]):
    ha=tf(xa,a[0].astype(float)); hb=tf(xb,b[0].astype(float))
    d=20*np.log10(np.abs(hb)/np.abs(ha)); m=FREQS<16000
    worst=FREQS[m][np.argmax(np.abs(d[m]))]
    res.append((kind,kw,float(np.sqrt(np.mean(d[m]**2))),float(np.abs(d[m]).max()),float(worst)))
for kind,kw,rms,mx,w in res:
    flag=" <==" if mx>1 else ""
    print(f"{kind:9s} {' '.join(f'{k}={v}' for k,v in kw.items()):28s} rms {rms:5.2f} dB max {mx:5.2f} dB at {w:7.0f} Hz{flag}")
