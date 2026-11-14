#!/usr/bin/env python3
"""LFO speed law: square LFO on pitch (key sync), the period from the
pitch flip times, original vs ours. Fit: Hz = 2^(speed / 12.0367) / 12
(speed 0 = a 12 s cycle), 0.02 % at speeds 0..100.

  measure_lfo_speed.py
"""
import numpy as np, s1, instfreq
sr=44100
for sp in (0,20,40,60,80,100,120,127):
    hz=0.084*2**(sp/12.05); L=min(30.0, max(2.0, 6.5/hz)); 
    spec=[{"native":s1.patch(p0=0,p91=1,p57=1,p41=2,p42=2,p43=sp,p44=20,p68=1),"notes":((0,L,69,127),),"length":L}]
    res=[]
    for ours in (False,True):
        (x,_),=s1.render_many(spec,ours=ours)
        c=instfreq.inst_cents(x[:1],sr,440.0,0.0005); s=c>0
        flips=np.nonzero(np.diff(s.astype(int))!=0)[0]
        flips=flips[flips>200]
        per=2*np.median(np.diff(flips))/sr if len(flips)>3 else np.nan
        res.append((per,flips[:3]/sr))
    print(f"speed {sp:3d}: period s1 {res[0][0]*1000:9.3f} ms ours {res[1][0]*1000:9.3f} ms ratio {res[0][0]/res[1][0]:.5f}  first flips s1 {np.round(res[0][1]*1000,2)} ours {np.round(res[1][1]*1000,2)}")
