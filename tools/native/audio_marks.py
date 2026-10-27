"""Cut the moments marked with F10 out of an --audio_dump_raw capture (audio_capture.cmd).

usage: python audio_marks.py LOG RAW [before_s=8] [after_s=1]
The dump starts with the first SDL callback, and the SDL driver logs "audio: X of 1875 frames"
after every 1875 frames of 256 samples (10 s at 48 kHz), so dump time 0 = first such line - 10 s.
For each "[native] F10" line: the 100 ms level profile of the seconds before it, the loudest
stretch against the clip's median, and clip_<n>.wav (16-bit stereo) next to RAW.
"""
import re, sys, wave
from datetime import datetime
import numpy as np

log, raw = sys.argv[1], sys.argv[2]
before = float(sys.argv[3]) if len(sys.argv) > 3 else 8.0
after = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
RATE = 48000
stamp = re.compile(rb'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]')


def when(line):
    m = stamp.match(line)
    return datetime.strptime(m.group(1).decode(), '%Y-%m-%d %H:%M:%S.%f').timestamp() if m else None


first_audio, marks = None, []
with open(log, 'rb') as f:
    for line in f:
        if first_audio is None and b'[apu]' in line and b'frames were silence' in line:
            first_audio = when(line)
        elif b'[native] F10:' in line:
            marks.append(when(line))
if first_audio is None:
    sys.exit('no "audio: ... frames" line in the log')
t0 = first_audio - 10.0
x = np.fromfile(raw, dtype='<f4')
x = x[: len(x) // 2 * 2].reshape(-1, 2)
print(f'{raw}: {len(x) / RATE:.1f} s; {len(marks)} F10 marks')
win = RATE // 10
for n, m in enumerate(marks):
    a, b = int((m - t0 - before) * RATE), int((m - t0 + after) * RATE)
    a, b = max(a, 0), min(b, len(x))
    if b - a < win:
        print(f'mark {n}: outside the capture')
        continue
    clip = x[a:b]
    k = len(clip) // win
    rms = np.sqrt((clip[: k * win] ** 2).mean(axis=1).reshape(k, win).mean(axis=1) + 1e-12)
    db = 20 * np.log10(rms)
    peak = np.abs(clip).max()
    loud = int(np.argmax(db))
    print(f'mark {n} at dump {m - t0:.1f} s: median {np.median(db):.1f} dBFS, loudest {db[loud]:.1f} at '
          f'{loud / 10 - before:+.1f} s, peak {20 * np.log10(peak + 1e-12):.1f} dBFS, clipped {(np.abs(clip) >= 0.999).sum()}')
    print('  dB per 100 ms:', ' '.join(f'{v:.0f}' for v in db))
    out = raw.rsplit('.', 1)[0] + f'_clip_{n}.wav'
    with wave.open(out, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((np.clip(clip, -1, 1) * 32767).astype('<i2').tobytes())
    print('  ->', out)
