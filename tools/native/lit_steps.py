"""Per-draw steps of a native frame dump's snapshotted target (gtav_native_dump_rt, default 2D0).

usage: python lit_steps.py DUMP_DIR [from_resolve] [to_resolve] [grid]
For each "-> snapshot N after this draw" between the two resolves (default: all), prints the draw
(prim, count, vs/ps, depth control, stencil ref) and how much of the target it changed (share of
pixels, bounding box), then writes steps_<from>_<to>.png: every snapshot that changed something,
with the changed pixels outlined in red. grid=1 overlays the 16 px lighting-tile grid.
"""
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw

D = sys.argv[1]
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
grid = len(sys.argv) > 4 and sys.argv[4] == '1'


def ppm(path):
    data = open(path, 'rb').read()
    head = data.split(b'\n', 3)
    w, h = map(int, head[1].split())
    return np.frombuffer(head[3], np.uint8)[:w * h * 3].reshape(h, w, 3)


# Snapshot index -> draw line, inside the resolve range.
steps, draw, resolve = [], None, 0
for line in open(f'{D}/commands.txt'):
    m = re.match(r'resolve #(\d+)', line)
    if m:
        resolve = int(m.group(1))
        continue
    if line.startswith('draw '):
        draw = line
        continue
    m = re.match(r'\s+-> snapshot (\d+) after this draw', line)
    if m and draw and lo <= resolve < hi:
        f = dict(re.findall(r'(\w+)=(\S+)', draw))
        steps.append((int(m.group(1)), resolve, f))

# Dump slots are one counter shared by resolves and snapshots: snapshot N is <N>_EEEE....ppm.
snaps = {int(f[:3]): f for f in os.listdir(D) if re.match(r'\d{3}_EEEE[0-9A-F]{4}_f\d+_\d+x\d+\.ppm$', f)}
prev, tiles = None, []
for n, res, f in steps:
    if n not in snaps:
        continue
    img = ppm(f'{D}/{snaps[n]}')
    img = img[:img.shape[0] // 8 * 8]
    if prev is None or prev.shape != img.shape:
        prev = img
        print(f'snap {n:3} (after R{res}) {snaps[n]}: first')
        continue
    changed = np.abs(img.astype(np.int16) - prev).max(2) > 4
    share = changed.mean() * 100
    box = ''
    if changed.any():
        ys, xs = np.nonzero(changed)
        box = f'box {xs.min()},{ys.min()}-{xs.max()},{ys.max()}'
    print(f'snap {n:3} R{res} prim={f.get("prim")} n={f.get("n"):>5} vs={f.get("vs", "")[:8]} '
          f'ps={f.get("ps", "")[:8]} dc={f.get("dc")} sref={f.get("sref")}: {share:5.1f}% {box}')
    if share > 0.05:
        tile = Image.fromarray(img.copy())
        dr = ImageDraw.Draw(tile)
        edge = changed & ~np.roll(changed, 1, 0) | changed & ~np.roll(changed, 1, 1)
        arr = np.array(tile)
        arr[edge] = (255, 0, 0)
        tile = Image.fromarray(arr)
        dr = ImageDraw.Draw(tile)
        if grid:  # snapshots are half resolution: 16 px tiles are 8 px here
            s = 16 * tile.width // 1280
            for x in range(0, tile.width, s):
                dr.line([(x, 0), (x, tile.height)], fill=(0, 80, 0))
            for y in range(0, tile.height, s):
                dr.line([(0, y), (tile.width, y)], fill=(0, 80, 0))
        dr.text((3, 2), f'{n} vs {f.get("vs", "")[:8]} {share:.1f}%', fill=(255, 255, 0))
        tiles.append(tile)
    prev = img

if tiles:
    cols = 3
    tw, th = tiles[0].size
    sheet = Image.new('RGB', (cols * tw, (len(tiles) + cols - 1) // cols * th), (40, 40, 40))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % cols) * tw, (i // cols) * th))
    out = f'{D}/steps_{lo}_{hi if hi < 1 << 30 else "end"}.png'
    sheet.save(out)
    print(out)
