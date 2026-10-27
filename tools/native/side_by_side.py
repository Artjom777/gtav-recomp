"""Native | emulated | difference image of one resolve slot.
usage: python side_by_side.py NATIVE_DUMP_DIR EMU_FRAME_DIR SLOT [gain]"""
import os, re, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_raw as cr

nd, ed, slot = sys.argv[1], sys.argv[2], int(sys.argv[3])
gain = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
words = {}
for line in open(f'{nd}/index.txt'):
    m = re.match(r'(\d+) addr=(\w+) .* words=([\w,]+)', line)
    if m:
        words[int(m.group(1))] = [int(v, 16) for v in m.group(3).split(',')]
files = []
for f in sorted(os.listdir(ed)):
    m = re.match(r'(\d+)_([0-9A-F]{8})\.bin$', f)
    if m:
        files.append((int(m.group(1)), int(m.group(2), 16)))
raw = [f for f in os.listdir(nd) if f.startswith(f'{slot:03}_') and f.endswith('.raw')][0]
m = re.match(r'(\d+)_([0-9A-F]{8})_f(\d+)_(\d+)x(\d+)_r(\d+)\.raw$', raw)
addr, fmt, w, h, row = m.group(2), *map(int, m.groups()[2:])
g, count = 0, -1
for line in open(f'{nd}/commands.txt'):
    m = re.match(r'resolve #(\d+) .*-> ([0-9A-F]{8}) L(\d+) F(\d+) .* at (\d+),(\d+)', line)
    if m and m.group(2) == addr:
        if not (int(m.group(3)) or int(m.group(4)) or int(m.group(6))):
            count += 1
        if int(m.group(1)) == slot:
            g = count
            break
buf = cr.emu_image(ed, files, g, addr, words[slot])
step = max(1, w // 320)
W, H = w // step, h // step
pts = [(i * step, j * step) for j in range(H) for i in range(W)]
nat = cr.native_texels(f'{nd}/{raw}', fmt, w, h, row, pts)[:, :3]
emu = np.array([(cr.texel(buf, words[slot], x, y) + [0, 0, 0])[:3] for x, y in pts])
if nat.shape[1] < 3:
    nat = np.pad(nat, ((0, 0), (0, 3 - nat.shape[1])))
nat, emu = nat.reshape(H, W, 3), emu.reshape(H, W, 3)
d = (nat - emu).mean(2)
dimg = np.stack([np.clip(d * 30, 0, 1), np.clip(-d * 30, 0, 1), np.zeros_like(d)], 2)
img = np.concatenate([np.clip(nat * gain, 0, 1), np.clip(emu * gain, 0, 1), dimg], 1)
out = f'{nd}/sbs_{slot:03}.png'
Image.fromarray((img * 255).astype(np.uint8)).save(out)
print(out, 'group', g)
