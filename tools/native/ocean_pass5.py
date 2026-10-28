"""Exact CPU reference of the ocean simulation's last pass (ps BCFF0D3F) on consecutive native dumps.

usage: python3 ocean_pass5.py DUMP_FRAME_N DUMP_FRAME_N+1
Pass 5 writes the height field (0C098000 or 0C097000): for output pixel (X, Y) it computes a texel
index L from X, Y (floor(uv * 128) and 32x32 block / 8x2 tile arithmetic, from the HLSL) and outputs
clamp(h[L] + P0.x * v[L], -2000, 2000), h = the previous frame's pass-5 output, v = this frame's
pass-4 output. The script compares the native output with the reference for exact pixel
coordinates and reports where they differ, plus whether the reference with X-1 / Y-1 (a floor that
flipped at an exact integer) explains those pixels.
"""
import os, re, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from compare_constants import load

N = 128


def resolves(d):
    """[(index, address, path)] of the 128x128 ocean resolves, in order."""
    out = []
    for n in sorted(os.listdir(d)):
        m = re.match(r'(\d+)_(0C0[89][78]000)_f34_128x128_r128\.raw$', n)
        if m:
            out.append((int(m.group(1)), m.group(2), f'{d}/{n}'))
    return out


def read(path):
    return np.fromfile(path, '<f4', count=N * N).reshape(N, N).astype(np.float64)


def index(X, Y):
    """Texel index L for output pixel (X, Y), following the HLSL step by step."""
    by, bx = Y // 32, X // 32
    b = by * 4 + bx
    lx, ly = X - 32 * bx, Y - 32 * by
    hy, qx = ly // 2, lx // 4
    sel_alt = 8 * hy + 8 * np.mod(qx / 8 + 0.5, 1.0)
    sel = np.where(8 * np.mod(hy / 8, 1.0) > 3.5, sel_alt, qx + 8 * hy)
    return (1024 * b + 8 * sel + (lx & 3) + 4 * (ly & 1)).astype(np.int64)


def pass5(h, v, dt, X, Y):
    L = index(X, Y)
    tx, ty = L % N, L // N
    return np.clip(h[ty, tx] + dt * v[ty, tx], -2000, 2000)


def p0x(d):
    for p, c, k, hsh in load(f'{d}/draws.bin'):
        if p == 8 and abs(k[4 * (256 + 225)] - 0.5) < 1e-6 and abs(k[4 * (256 + 224) + 3] - 17.1) < 1e-4:
            return k[4 * (256 + 224)]
    return 0.05


def main():
    a, b = sys.argv[1], sys.argv[2]
    ra, rb = resolves(a), resolves(b)
    h = read(ra[-1][2])          # frame N pass 5 output (height)
    v = read(rb[-2][2])          # frame N+1 pass 4 output (velocity)
    out = read(rb[-1][2])        # frame N+1 pass 5 output
    dt = p0x(b)
    Y, X = np.mgrid[0:N, 0:N]
    ref = pass5(h, v, dt, X, Y)
    err = np.abs(out - ref)
    tol = 1e-3 * np.maximum(1, np.abs(ref))
    bad = err > tol
    print(f'dt {dt}; inputs {ra[-1][1]} (N), {rb[-2][1]} (N+1); output {rb[-1][1]}')
    print(f'exact reference: {bad.sum()} of {N*N} pixels differ (max {err.max():.4g}); out range '
          f'{out.min():+.2f}..{out.max():+.2f}, ref range {ref.min():+.2f}..{ref.max():+.2f}')
    if bad.any():
        for dx, dy in ((-1, 0), (0, -1), (-1, -1)):
            alt = pass5(h, v, dt, np.maximum(X + dx, 0), np.maximum(Y + dy, 0))
            hit = bad & (np.abs(out - alt) <= tol)
            print(f'   explained by floor flip ({dx:+d},{dy:+d}): {hit.sum()}')
        ys, xs = np.nonzero(bad)
        print('   first bad pixels (x,y):', list(zip(xs[:12].tolist(), ys[:12].tolist())))
        print('   bad columns histogram (x mod 32):', np.bincount(xs % 32, minlength=32).tolist())


if __name__ == '__main__':
    main()
