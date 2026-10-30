#!/usr/bin/env python3
# fps_from_log.py <log> [skip]: fps per 300 presented frames from the "[native] presented N frames" lines,
# with record/wait times and the record breakdown (setup consts textures streams pipeline bind+draw draws).
import re,sys,datetime
# fps per 300 presented frames + record/wait/draws
pat=re.compile(r'^\[(\S+ \S+)\].*presented (\d+) frames .*record avg ([\d.]+) max ([\d.]+) ms, wait for slot avg ([\d.]+) max ([\d.]+)')
dr=re.compile(r'record ms per frame: setup ([\d.]+) consts ([\d.]+) textures ([\d.]+) streams ([\d.]+) pipeline ([\d.]+) bind\+draw ([\d.]+) \(draws only; (\d+) draws')
prev=None; rec=None; rows=[]
for l in open(sys.argv[1],errors='replace'):
    m=dr.search(l)
    if m: rec=m.groups(); continue
    m=pat.search(l)
    if not m: continue
    t=datetime.datetime.strptime(m.group(1),'%Y-%m-%d %H:%M:%S.%f]') if m.group(1).endswith(']') else datetime.datetime.strptime(m.group(1),'%Y-%m-%d %H:%M:%S.%f')
    n=int(m.group(2))
    if prev:
        dt=(t-prev[0]).total_seconds(); fps=(n-prev[1])/dt if dt>0 else 0
        rows.append((n,fps,m.group(3),m.group(4),m.group(5),rec))
    prev=(t,n)
skip=int(sys.argv[2]) if len(sys.argv)>2 else 0
for r in rows[skip:]:
    print(f"{r[0]:6d} fps {r[1]:5.1f} rec {r[2]:>5}/{r[3]:>6} wait {r[4]:>5}", (' '.join(r[5]) if r[5] else ''))
v=[r[1] for r in rows[skip:] if r[5] and int(r[5][6])>500]
if v: print("heavy frames: mean %.1f min %.1f n=%d"%(sum(v)/len(v),min(v),len(v)))
