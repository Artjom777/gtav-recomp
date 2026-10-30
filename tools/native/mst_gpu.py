#!/usr/bin/env python3
# mst_gpu.py <metal-gpu-intervals export.xml>: per command buffer (frame) GPU encoders from a Metal
# System Trace (xctrace export --xpath '.../table[@schema="metal-gpu-intervals"]'): encoder count,
# busy time per channel, gaps, and the encoders by cost in a typical frame.
import sys, xml.etree.ElementTree as ET, collections
vals = {}
def val(e):
    r = e.get('ref')
    if r is not None: return vals.get(r)
    v = e.get('fmt') if e.text is None and len(e) else (e.text or e.get('fmt'))
    if e.tag in ('formatted-label',):
        v = e.get('fmt')
    if e.get('id') is not None: vals[e.get('id')] = v
    for c in e: val(c)
    return v
rows = []
for ev, e in ET.iterparse(sys.argv[1], events=('end',)):
    if e.tag != 'row': continue
    c = list(e)
    v = [val(x) for x in c]
    # columns: start duration channel frame latency depth label state uuid color process gpu subtitle ios bytes cmdbuf encoder subid
    try:
        rows.append(dict(start=int(v[0]), dur=int(v[1]), ch=v[2], depth=v[5], label=v[6], cb=v[15], enc=v[16]))
    except Exception:
        pass
    e.clear()
rows = [r for r in rows if r['depth'] in ('0', 0, None)]
print(len(rows), 'intervals; channels', collections.Counter(r['ch'] for r in rows))
by_cb = collections.defaultdict(list)
for r in rows: by_cb[r['cb']].append(r)
frames = []
for cb, rs in by_cb.items():
    rs.sort(key=lambda r: r['start'])
    t0 = rs[0]['start']; t1 = max(r['start'] + r['dur'] for r in rs)
    encs = collections.OrderedDict()
    for r in rs: encs.setdefault(r['enc'], []).append(r)
    busy = collections.Counter()
    for r in rs: busy[r['ch']] += r['dur']
    frames.append((t0, cb, len(encs), (t1 - t0) / 1e6, {k: v / 1e6 for k, v in busy.items()}, encs))
frames.sort()
for f in frames[:40]:
    print(f"cb {f[1]}: {f[2]:4d} encoders, span {f[3]:6.2f} ms, busy " + ' '.join(f"{k} {v:.2f}" for k, v in f[4].items()))
big = max(frames, key=lambda f: f[2])
print('\nlargest command buffer', big[1], 'encoders in order: index, channels and ms, gap before (us)')
prev_end = None
for i, (enc, rs) in enumerate(big[5].items()):
    s = min(r['start'] for r in rs); e_ = max(r['start'] + r['dur'] for r in rs)
    gap = (s - prev_end) / 1e3 if prev_end else 0
    prev_end = e_
    print(f"{i:4d} {(e_ - s)/1e3:8.1f} us span  " + ' '.join(f"{r['ch'][:4]} {r['dur']/1e3:.1f}" for r in rs) + f"  gap {gap:.1f}")
