"""CPU reference of the ocean wave simulation's first pass (ps 152069AE) against native and emulator.

usage: python3 ocean_ref.py NATIVE_DUMP_DIR EMU_DUMP_ROOT
The pass (from the translated HLSL; d = 1/128, P0/P1 = UpdateParams0/1, UpdateOffset.zw = 0):
  avg  = 0.18 * (4 orthogonal neighbours of cur) + 0.07 * (4 diagonal neighbours)
  out  = 2^(-0.5146 * P0.x) * (clamp(prev) + noise - 0.5 * P0.x * cur - 40 * P0.x * (cur - avg))
with cur = s13 (0C097000), prev = s14 (0C087000), noise = a term from the s11 noise texture.
The residual out - model(noise=0) is the noise term: smooth and small where the inputs and the
sampling are right. Each tap's texel offset is also fitted to show a sampling shift.
"""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from compare_dump import tiled_offset
from compare_constants import load

N = 128
OFFS = np.array([tiled_offset(x, y, N, 2) for y in range(N) for x in range(N)])


def guest_f32(raw):
    """Tiled big-endian 128x128 R32F guest surface -> [y, x]."""
    a = np.frombuffer(raw, '>f4', count=len(raw) // 4)
    return a[OFFS // 4].reshape(N, N).astype(np.float64)


def native_f32(path):
    return np.fromfile(path, '<f4', count=N * N).reshape(N, N).astype(np.float64)


def shift(a, dx, dy):
    """a sampled at (x + dx, y + dy), clamped (the fetch constants clamp: c660)."""
    ys = np.clip(np.arange(N) + dy, 0, N - 1)
    xs = np.clip(np.arange(N) + dx, 0, N - 1)
    return a[np.ix_(ys, xs)]


def model(cur, prev, p0, ox=0, oy=0):
    s = lambda dx, dy: shift(cur, dx + ox, dy + oy)
    avg = 0.18 * (s(0, -1) + s(0, 1) + s(1, 0) + s(-1, 0)) + 0.07 * (s(1, 1) + s(1, -1) + s(-1, 1) + s(-1, -1))
    c = s(0, 0)
    pv = np.clip(shift(prev, ox, oy), -2000, 2000)
    return 2.0 ** (-0.5146 * p0) * (pv - 0.5 * p0 * c - 40 * p0 * (c - avg))


def report(name, out, cur, prev, p0):
    print(f'{name}: out range {out.min():+.3f}..{out.max():+.3f} mean {out.mean():+.4f}; '
          f'cur {cur.min():+.3f}..{cur.max():+.3f}; prev {prev.min():+.3f}..{prev.max():+.3f}')
    best = None
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            r = out - model(cur, prev, p0, ox, oy)
            rough = np.abs(np.diff(r, axis=1)).mean() + np.abs(np.diff(r, axis=0)).mean()
            print(f'   tap offset ({ox:+d},{oy:+d}): residual mean {r.mean():+.4f} std {r.std():.4f} roughness {rough:.4f}')
            if best is None or r.std() < best[0]:
                best = (r.std(), ox, oy)
    print(f'   best offset {best[1:]}')


def main():
    nd, root = sys.argv[1], sys.argv[2]
    draws = load(f'{nd}/draws.bin')
    # The first pass-1 draw: pixel shader c224 = UpdateParams0 = (0.05, time, ...).
    p0 = None
    for p, c, k, h in draws:
        if p == 8 and abs(k[4 * (256 + 225)] - 0.5) < 1e-6 and abs(k[4 * (256 + 224) + 3] - 17.1) < 1e-4:
            p0 = k[4 * (256 + 224)]
            break
    print('P0.x', p0)
    names = sorted(os.listdir(nd))
    res = [n for n in names if n.endswith('.raw') and ('_0C087000_' in n or '_0C097000_' in n)]
    cur = guest_f32(open(f'{nd}/tex_0C097000_f36.bin', 'rb').read()[24:])
    prev = guest_f32(open(f'{nd}/tex_0C087000_f36.bin', 'rb').read()[24:])
    out = native_f32(f'{nd}/{res[0]}')
    report(f'native {res[0]}', out, cur, prev, p0)
    # Emulator: inputs = the previous frame's last 0C097000 / 0C087000 outputs.
    frames = sorted(d for d in os.listdir(root) if d.startswith('frame_'))
    for a, b in zip(frames, frames[1:]):
        fa, fb = sorted(os.listdir(f'{root}/{a}')), sorted(os.listdir(f'{root}/{b}'))
        c97 = [f for f in fa if '_0C097000' in f]
        c87 = [f for f in fa if '_0C087000' in f]
        o87 = [f for f in fb if '_0C087000' in f]
        if not (c97 and c87 and o87):
            continue
        ecur = guest_f32(open(f'{root}/{a}/{c97[-1]}', 'rb').read())
        eprev = guest_f32(open(f'{root}/{a}/{c87[-1]}', 'rb').read())
        eout = guest_f32(open(f'{root}/{b}/{o87[0]}', 'rb').read())
        same_in = np.abs(ecur - cur).max(), np.abs(eprev - prev).max()
        print(f'emu {b} (inputs from {a}; |cur-native cur| {same_in[0]:.4g}, |prev-native prev| {same_in[1]:.4g})')
        report(f'emu {b} {o87[0]}', eout, ecur, eprev, p0)


if __name__ == '__main__':
    main()
