"""Loud-burst finder for tools/native/audio_boot_capture.sh runs.

usage: python audio_bursts.py logs/NAME.log [--db 12]
Reads logs/NAME_mix.raw (device mix, float32, channel count from the log) and logs/NAME_xma.raw
(decoded XMA frames with steady-clock ms). For every burst in the mix (100 ms window more than
--db above the median), prints the per-channel levels and the XMA contexts that decoded audio in
the 400 ms before it, with each frame's level against that context's own median. A context that
is loud right there means the decoder; none means the game's mixer applied the gain.
"""
import re
import sys
from collections import defaultdict

import numpy as np

log = sys.argv[1]
thresh = float(sys.argv[sys.argv.index('--db') + 1]) if '--db' in sys.argv else 12.0
base = log[:-4]
text = open(log, errors='replace').read()
start_ms = int(re.search(r'audio dump starts at steady ms (\d+)', text).group(1))
m = re.search(r'submitting (\d) ch', text)
chans = int(m.group(1)) if m else 2
R = 48000

x = np.fromfile(base + '_mix.raw', dtype='<f4')
x = x[: len(x) // chans * chans].reshape(-1, chans)
w = R // 10
n = len(x) // w
rms = np.sqrt((x[: n * w] ** 2).reshape(n, w, chans).mean(1)) + 1e-9
tot = 20 * np.log10(np.sqrt((rms ** 2).sum(1)))
med = np.median(tot[tot > -90]) if (tot > -90).any() else -90
print(f'mix: {len(x) / R:.1f} s, {chans} ch, median {med:.1f} dB')

# XMA frames: header {ctx, rate, channels, samples, ms}, then samples*channels BE int16.
raw = open(base + '_xma.raw', 'rb').read()
frames = []  # (t_s, ctx, rate, ch, peak, rms_db)
pos = 0
while pos + 20 <= len(raw):
    ctx, rate, ch, ns, ms = np.frombuffer(raw, '<u4', 5, pos)
    pos += 20
    if pos + int(ns * ch) * 2 > len(raw):
        break  # the run was killed mid-write
    pcm = np.frombuffer(raw, '>i2', int(ns * ch), pos).astype(np.float32) / 32768
    pos += int(ns * ch) * 2
    r = 20 * np.log10(np.sqrt((pcm ** 2).mean()) + 1e-9)
    frames.append(((int(ms) - start_ms) / 1000, int(ctx), int(rate), int(ch), float(np.abs(pcm).max()), r))
print(f'xma: {len(frames)} frames, {len({f[1] for f in frames})} contexts')
by_ctx = defaultdict(list)
for f in frames:
    by_ctx[f[1]].append(f[5])
ctx_med = {c: float(np.median(v)) for c, v in by_ctx.items()}

idx = np.where(tot > med + thresh)[0]
groups = []
for i in idx:
    if groups and i - groups[-1][-1] <= 3:
        groups[-1].append(i)
    else:
        groups.append([i])
print(f'{len(groups)} bursts > {thresh} dB over the median')
names = ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR'][:chans] if chans == 6 else ['L', 'R']
times = np.array([f[0] for f in frames])
for g in groups:
    k = g[int(np.argmax(tot[g]))]
    t0 = g[0] / 10
    seg = x[k * w:(k + 1) * w]
    c = np.corrcoef(seg[:, 0], seg[:, 1])[0, 1]
    print(f'\nburst t={t0:.1f}s len {len(g) / 10:.1f}s peak {tot[k]:.1f} dB (corr L/R {c:.3f}): '
          + ' '.join(f'{nm} {20 * np.log10(rms[k, j]):.1f}' for j, nm in enumerate(names)))
    sel = np.where((times >= t0 - 0.4) & (times <= t0 + len(g) / 10))[0]
    per = defaultdict(list)
    for i in sel:
        per[frames[i][1]].append(frames[i])
    rows = sorted(per.items(), key=lambda kv: -max(f[5] - ctx_med[kv[0]] for f in kv[1]))
    for ctx, fs in rows[:6]:
        best = max(fs, key=lambda f: f[5])
        print(f'   ctx {ctx:3d} {best[2]} Hz {best[3]}ch: {len(fs)} frames, max rms {best[5]:.1f} dB '
              f'(ctx median {ctx_med[ctx]:.1f}, +{best[5] - ctx_med[ctx]:.1f}), peak {best[4]:.2f}, '
              f'first frame at {fs[0][0] - t0:+.2f}s')
