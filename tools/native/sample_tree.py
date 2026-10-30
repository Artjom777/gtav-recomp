#!/usr/bin/env python3
# sample_tree.py <macOS sample output> [min samples] [max depth] [thread filter...]: call trees of
# the native render thread (Renderer::ThreadMain), Main XThread and GPU Commands (or the threads
# whose header contains a filter), down to nodes with at least `min` samples.
import re, sys
L = open(sys.argv[1]).read().split('\n')
mn = int(sys.argv[2]) if len(sys.argv) > 2 else 20
md = int(sys.argv[3]) if len(sys.argv) > 3 else 30
filt = sys.argv[4:]
def depth(l): return len(re.match(r'^[ +!:|]*', l).group(0))
starts = [i for i, l in enumerate(L) if re.match(r'^    \d+ Thread_', l)]
for si, s in enumerate(starts):
    e = starts[si + 1] if si + 1 < len(starts) else len(L)
    blk = L[s:e]
    hit = any(f in L[s] for f in filt) if filt else (
        any('Renderer::ThreadMain' in l for l in blk) or 'Main XThread' in L[s] or 'GPU Commands' in L[s])
    if not hit: continue
    print('=====', L[s].strip()[:80])
    d0 = depth(blk[1])
    for l in blk[1:]:
        if not l.strip(): break
        dd = depth(l) - d0
        m = re.search(r'(\d+) (.+?)  \(in ', l)
        if m and dd <= md and int(m.group(1)) >= mn:
            print(' ' * (dd // 2) + m.group(1) + ' ' + re.sub(r'\(.*', '', m.group(2))[:90])
