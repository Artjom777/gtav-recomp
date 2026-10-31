#!/usr/bin/env python3
# mst_labels.py <encoders-list.xml> <gpu-intervals.xml>: joins MoltenVK's encoder labels
# (metal-application-encoders-list) with the GPU intervals (metal-gpu-intervals) of a Metal
# System Trace: per label kind, encoders per frame, GPU busy, and the idle gap before them.
import sys, re, collections, xml.etree.ElementTree as ET
def rows(path):
    vals = {}; cols = []
    def val(e):
        r = e.get('ref')
        if r is not None: return vals.get(r)
        v = e.text if (e.text and e.text.strip()) else e.get('fmt')
        if e.tag not in ('row',) and e.get('id') is not None: vals[e.get('id')] = v
        for c in e: val(c)
        return v
    for ev, e in ET.iterparse(path, events=('end',)):
        if e.tag == 'mnemonic': cols.append(e.text)
        if e.tag != 'row': continue
        v = [val(x) for x in e]
        yield dict(zip(cols, v)), [x for x in e]
        e.clear()
labels = {}
for r, _ in rows(sys.argv[1]):
    enc = r.get('encoder-id'); lab = r.get('encoder-label')
    if enc and lab: labels[enc] = re.sub(r'^\[\d+\] ', '', lab)
ivs = []
for r, raw in rows(sys.argv[2]):
    try:
        ivs.append((int(raw[0].text or 0) if raw[0].text else int(r['start'] or 0), r))
    except Exception: pass
by_cb = collections.defaultdict(list)
for _, r in ivs:
    if r.get('event-depth') not in ('0', None): continue
    by_cb[r.get('cmdbuffer-id')].append(r)
big = [rs for rs in by_cb.values() if len({x.get('encoder-id') for x in rs}) > 150]
stat = collections.defaultdict(lambda: [0, 0.0, 0.0])
for rs in big:
    encs = collections.OrderedDict()
    for x in sorted(rs, key=lambda x: int(x['start'])): encs.setdefault(x.get('encoder-id'), []).append(x)
    prev_end = None
    for enc, xs in encs.items():
        s = min(int(x['start']) for x in xs); e = max(int(x['start']) + int(x['duration']) for x in xs)
        lab = labels.get(enc, '?')
        st = stat[lab]; st[0] += 1; st[1] += sum(int(x['duration']) for x in xs) / 1e3
        if prev_end is not None and s > prev_end: st[2] += (s - prev_end) / 1e3
        prev_end = max(prev_end or 0, e)
n = len(big)
print(f'{n} frames; per frame: encoders, GPU busy us, idle gap before them us')
for lab, (c, b, g) in sorted(stat.items(), key=lambda kv: -kv[1][2]):
    print(f'{c / n:7.1f} {b / n:9.0f} {g / n:9.0f}  {lab}')
