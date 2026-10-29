"""Loudness report of a --audio_dump_raw capture (float32, interleaved, 48 kHz).

usage: python audio_levels.py DUMP.raw [channels=2]
The dump has the device's channel count: 6 when the log says "submitting 6 ch" (this laptop's
speakers). Reading a 6-channel dump as stereo invents a 16 kHz (48k/3) "whine".
Prints one screen: overall level, clipped samples, the loudest/quietest stretches and every
sudden jump of more than 12 dB between neighbouring 100 ms windows.
"""
import sys
import numpy as np

path = sys.argv[1]
channels = int(sys.argv[2]) if len(sys.argv) > 2 else 2
rate = 48000
x = np.fromfile(path, dtype='<f4')
x = x[: len(x) // channels * channels].reshape(-1, channels)
secs = len(x) / rate
print(f'{path}: {secs:.1f} s, {channels} ch')
if not len(x):
    sys.exit()
mono = np.abs(x).max(axis=1)
clipped = int((mono >= 0.999).sum())
win = rate // 10
n = len(x) // win
rms = np.sqrt((x[: n * win] ** 2).mean(axis=1).reshape(n, win).mean(axis=1) + 1e-12)
db = 20 * np.log10(rms)
peak = mono[: n * win].reshape(n, win).max(axis=1)
print(f'level: median {np.median(db):.1f} dBFS, 95th pct {np.percentile(db, 95):.1f}, max {db.max():.1f}; '
      f'clipped samples {clipped} ({clipped / len(x) * 100:.3f} %), windows with clipping {(peak >= 0.999).sum()}')
silent = db < -70
print(f'silent windows (< -70 dBFS): {silent.sum()} of {n}')
jumps = np.where(np.abs(np.diff(db)) > 12)[0]
print(f'jumps > 12 dB between 100 ms windows: {len(jumps)}')
for j in jumps[:25]:
    print(f'  t={j / 10:7.1f} s  {db[j]:6.1f} -> {db[j + 1]:6.1f} dBFS')
order = np.argsort(db)[::-1]
print('loudest windows:', ', '.join(f'{i / 10:.1f}s {db[i]:.1f}' for i in order[:8]))
