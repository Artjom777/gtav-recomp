"""Exact CPU reference of the ocean simulation's first pass (ps 152069AE) on consecutive native dumps.

usage: python3 ocean_pass1.py DUMP_FRAME_N DUMP_FRAME_N+1
Inputs: h = frame N pass-5 output (s13), v = frame N pass-4 output (s14), the k_8 noise texture s11
(tex_<addr>_f2.bin of frame N+1, bilinear, wrap), constants c224/c225/c227 of frame N+1's pass-1
draw. Output compared: frame N+1's first ocean resolve. From the HLSL (d = 1/128):
  avg = 0.18 * orthogonal + 0.07 * diagonal neighbours of h (point, clamp)
  out = 2^(P0.x * c255.w) * (clamp(v) + P1.y*P0.x*(n1 - 0.49216) + P0.x*P0.w*(n2 - 0.49216)
                             - 0.5*P0.x*h - 40*P0.x*(h - avg))
  n1 = s11(P1.x*(UO.x/256 + u) + P0.y*c255.x, P0.z + P1.x*(UO.y/256 + v)).x, n2 = s11(4u + P1.w, 4v + P1.w).x
"""
import os, re, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from compare_constants import load
from compare_dump import tiled_offset
from ocean_pass5 import resolves, read, N

C255 = [struct.unpack('<f', struct.pack('<I', w))[0] for w in (0x3627C5AC, 0x42200000, 0x3F000000, 0xBF03BB12)]
C253W = struct.unpack('<f', struct.pack('<I', 0xBEFBFBFC))[0]


def noise_texture(d, addr):
    raw = open(f'{d}/tex_{addr}_f2.bin', 'rb').read()[24:]
    offs = np.array([tiled_offset(x, y, 128, 0) for y in range(128) for x in range(128)])
    return np.frombuffer(raw, np.uint8)[offs].reshape(128, 128).astype(np.float64) / 255.0


def bilinear_wrap(t, u, v):
    x, y = u * t.shape[1] - 0.5, v * t.shape[0] - 0.5
    x0, y0 = np.floor(x), np.floor(y)
    fx, fy = x - x0, y - y0
    x0, y0 = x0.astype(int), y0.astype(int)
    g = lambda yy, xx: t[np.mod(yy, t.shape[0]), np.mod(xx, t.shape[1])]
    return ((1 - fy) * ((1 - fx) * g(y0, x0) + fx * g(y0, x0 + 1)) + fy * ((1 - fx) * g(y0 + 1, x0) + fx * g(y0 + 1, x0 + 1)))


def consts(d):
    for p, c, k, _ in load(f'{d}/draws.bin'):
        reg = lambda r: k[4 * (256 + r):4 * (256 + r) + 4]
        if p == 8 and abs(reg(225)[0] - 0.5) < 1e-6 and abs(reg(224)[3] - 17.1) < 1e-4:
            return reg(224), reg(225), reg(227)
    raise SystemExit('no pass-1 draw')


def pass1(h, v, noise, P0, P1, UO, xshift=0.0, yshift=0.0):
    Y, X = np.mgrid[0:N, 0:N]
    u, w = (X + xshift) / N, (Y + yshift) / N  # texel-edge coordinates (D3D9 pixel centres)
    pt = lambda a, dx, dy: a[np.clip(Y + dy, 0, N - 1), np.clip(X + dx, 0, N - 1)]
    avg = 0.18 * (pt(h, 0, -1) + pt(h, 0, 1) + pt(h, 1, 0) + pt(h, -1, 0)) + \
          0.07 * (pt(h, 1, 1) + pt(h, 1, -1) + pt(h, -1, 1) + pt(h, -1, -1))
    c = pt(h, 0, 0)
    n1 = bilinear_wrap(noise, P1[0] * (UO[0] / 256 + u) + P0[1] * C255[0], P0[2] + P1[0] * (UO[1] / 256 + w))
    n2 = bilinear_wrap(noise, 4 * u + P1[3], 4 * w + P1[3])
    acc = (np.clip(pt(v, 0, 0), -2000, 2000) + P1[1] * P0[0] * (n1 + C253W) + P0[0] * P0[3] * (n2 + C253W)
           - C255[2] * P0[0] * c - C255[1] * P0[0] * (c - avg))
    return 2.0 ** (P0[0] * C255[3]) * acc, n1, n2


def main():
    a, b = sys.argv[1], sys.argv[2]
    ra, rb = resolves(a), resolves(b)
    h, v, out = read(ra[-1][2]), read(ra[-2][2]), read(rb[0][2])
    P0, P1, UO = consts(b)
    s11 = re.search(r'ps=152069AE\S*.*? s11:T([0-9A-F]{8})', open(f'{b}/commands.txt').read()).group(1)
    noise = noise_texture(b, s11)
    print(f'P0 {P0}\nP1 {P1}\nUO {UO}\nnoise {s11} mean {noise.mean():.3f}')
    ref, n1, n2 = pass1(h, v, noise, P0, P1, UO)
    err = out - ref
    print(f'out range {out.min():+.3f}..{out.max():+.3f}; ref range {ref.min():+.3f}..{ref.max():+.3f}')
    print(f'error mean {err.mean():+.5f} std {err.std():.5f} max |{np.abs(err).max():.4g}|; '
          f'pixels off by >1e-3: {(np.abs(err) > 1e-3).sum()}')
    # Which term would explain the error?
    X = np.vstack([n1.ravel(), n2.ravel(), np.ones(err.size)]).T
    co, _, _, _ = np.linalg.lstsq(X, err.ravel(), rcond=None)
    print('error ~ %.4f n1 + %.4f n2 + %.4f' % tuple(co))


if __name__ == '__main__':
    main()
