"""Pair a native frame dump with the emulated renderer's resolves of the same frame.

usage: python3 compare_dump.py NATIVE_DUMP_DIR EMU_DUMP_DIR [first] [count]
Native resolve i is shown next to emulated resolve i (decoded with the destination fetch
constant the native dump recorded), with the mean absolute difference of their RGB.
"""
import os, re, struct, sys
from PIL import Image, ImageDraw


def tiled_offset(x, y, pitch, bpb_log2):
    # texture_util::GetTiledOffset2D (xenia / ReXGlue)
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpb_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            ((((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) + (offset & 0x3F))


def half(h):
    s, e, m = h >> 15, (h >> 10) & 31, h & 1023
    v = m * 2.0 ** -24 if e == 0 else 65504.0 if e == 31 else (m | 1024) * 2.0 ** (e - 25)
    return -v if s else v


def f32(w):
    return struct.unpack('<f', struct.pack('<I', w))[0]


def to8(v):
    return int(max(0.0, min(1.0, v)) * 255)


# xenos texture format -> (bytes per texel, decoder of the host-endian texel bytes -> RGB8)
FORMATS = {
    2: (1, lambda b: (b[0], b[0], b[0])),
    6: (4, lambda b: (b[0], b[1], b[2])),
    7: (4, lambda b: tuple(((struct.unpack('<I', b)[0] >> (10 * i)) & 1023) >> 2 for i in range(3))),
    54: (4, lambda b: tuple(((struct.unpack('<I', b)[0] >> (10 * i)) & 1023) >> 2 for i in range(3))),
    22: (4, lambda b: ((struct.unpack('<I', b)[0] >> 8) * 255 // 0xFFFFFF,) * 3),
    23: (4, lambda b: ((struct.unpack('<I', b)[0] >> 8) * 255 // 0xFFFFFF,) * 3),
    25: (4, lambda b: (to8(struct.unpack('<h', b[0:2])[0] / 32767 * 32), to8(struct.unpack('<h', b[2:4])[0] / 32767 * 32), 0)),
    29: (8, lambda b: tuple(to8(half(v)) for v in struct.unpack('<4H', b)[:3])),
    30: (2, lambda b: (to8(half(struct.unpack('<H', b)[0])),) * 3),
    31: (4, lambda b: tuple(to8(half(v)) for v in struct.unpack('<2H', b)) + (0,)),
    32: (8, lambda b: tuple(to8(half(v)) for v in struct.unpack('<4H', b)[:3])),
    36: (4, lambda b: (to8(f32(struct.unpack('<I', b)[0])),) * 3),
}


def decode(path, words, out_w=320, out_h=180):
    data = open(path, 'rb').read()
    fmt, endian = words[1] & 0x3F, (words[1] >> 6) & 3
    tiled = (words[0] >> 31) & 1
    pitch = ((words[0] >> 22) & 0x1FF) * 32
    w, h = (words[2] & 0x1FFF) + 1, ((words[2] >> 13) & 0x1FFF) + 1
    if fmt not in FORMATS:
        return None
    bpp, conv = FORMATS[fmt]
    pitch = max(pitch, w)
    img = Image.new('RGB', (out_w, out_h))
    px = img.load()
    log2 = bpp.bit_length() - 1
    for oy in range(out_h):
        for ox in range(out_w):
            x, y = ox * w // out_w, oy * h // out_h
            off = tiled_offset(x, y, pitch, log2) if tiled else (y * pitch + x) * bpp
            b = data[off:off + bpp]
            if len(b) < bpp:
                continue
            if endian == 1:
                b = bytes(b[i ^ 1] for i in range(bpp))
            elif endian == 2:
                b = b''.join(b[i:i + 4][::-1] for i in range(0, bpp, 4))
            elif endian == 3:
                b = b''.join(b[i + 2:i + 4] + b[i:i + 2] for i in range(0, bpp, 4))
            px[ox, oy] = conv(b)
    return img


def pick_emu_frame(nd, root):
    """Of a dumped window of emulated frames, the one whose draw constants match the native frame."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from compare_constants import load, best_emu_frame
    import io, contextlib
    nat = load(f'{nd}/draws.bin')
    with contextlib.redirect_stdout(io.StringIO()):
        emu = best_emu_frame(nat, root)
    best = None
    for d in sorted(os.listdir(root)):
        if os.path.exists(f'{root}/{d}/draws.bin') and load(f'{root}/{d}/draws.bin')[:5] == emu[:5]:
            best = d
    print('emulated frame:', best)
    return f'{root}/{best}'


def main():
    nd, ed = sys.argv[1], sys.argv[2]
    if not any(f.endswith('.bin') and f != 'draws.bin' for f in os.listdir(ed)):
        ed = pick_emu_frame(nd, ed)
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    index = {}
    for line in open(f'{nd}/index.txt'):
        m = re.match(r'(\d+) addr=(\w+) .* words=([\w,]+)', line)
        if m:
            index[int(m.group(1))] = (m.group(2), [int(x, 16) for x in m.group(3).split(',')])
    emu_files = sorted(f for f in os.listdir(ed) if f.endswith('.bin') and f != 'draws.bin')
    nat = sorted(f for f in os.listdir(nd) if re.match(r'\d+_[0-9A-F]+_f', f))
    # The two sequences drift (the emulator resolves things D3D-level capture never sees), so pair
    # each native resolve with the emulated resolve to the same address nearest in sequence.
    by_addr = {}
    for k, f in enumerate(emu_files):
        by_addr.setdefault(f.split('_')[1][:-4], []).append((k, f))
    emu = []
    for i, n in enumerate(nat):
        cands = by_addr.get(n.split('_')[1], [])
        emu.append(min(cands, key=lambda c: abs(c[0] - i))[1] if cands else None)
    rows = []
    for i in range(first, min(first + count, len(nat))):
        a = Image.open(f'{nd}/{nat[i]}').resize((320, 180))
        b = decode(f'{ed}/{emu[i]}', index[i][1]) if emu[i] and i in index else None
        diff = None
        if b:
            pa, pb = list(a.get_flattened_data()) if hasattr(a, 'get_flattened_data') else list(a.getdata()), list(b.getdata())
            diff = sum(abs(x[c] - y[c]) for x, y in zip(pa, pb) for c in range(3)) / (3 * len(pa))
        rows.append((i, nat[i], emu[i] or '-', a, b, diff))
        print(f'{i:03} native {nat[i][:-4]:40} emu {emu[i] or "-":22} diff {diff if diff is None else round(diff, 1)}')
    sheet = Image.new('RGB', (2 * 320 + 8, len(rows) * 196), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for k, (i, n, e, a, b, diff) in enumerate(rows):
        sheet.paste(a, (0, k * 196 + 14))
        if b:
            sheet.paste(b, (328, k * 196 + 14))
        d.text((2, k * 196), f'{i:03} {n[4:-4]}  vs emu {e}  diff {diff if diff is None else round(diff, 1)}', fill=(255, 255, 0))
    sheet.save(f'{nd}/compare_{first}.png')


if __name__ == '__main__':
    main()


def emu_texel(path, words, x, y):
    """Raw value tuple of one texel of an emulated resolve (for small_values.txt comparisons)."""
    data = open(path, 'rb').read()
    fmt, endian = words[1] & 0x3F, (words[1] >> 6) & 3
    tiled = (words[0] >> 31) & 1
    w = (words[2] & 0x1FFF) + 1
    pitch = max(((words[0] >> 22) & 0x1FF) * 32, w)
    bpp = {2: 1, 30: 2, 6: 4, 7: 4, 54: 4, 25: 4, 31: 4, 36: 4, 22: 4, 23: 4, 29: 8, 32: 8, 26: 8}.get(fmt)
    if not bpp:
        return None
    off = tiled_offset(x, y, pitch, bpp.bit_length() - 1) if tiled else (y * pitch + x) * bpp
    b = data[off:off + bpp]
    if endian == 1:
        b = bytes(b[i ^ 1] for i in range(bpp))
    elif endian == 2:
        b = b''.join(b[i:i + 4][::-1] for i in range(0, bpp, 4))
    if fmt in (29, 32):
        return tuple(round(half(v), 5) for v in struct.unpack('<4H', b))
    if fmt in (30, 31):
        return tuple(round(half(v), 5) for v in struct.unpack('<%dH' % (bpp // 2), b))
    if fmt == 36:
        return (round(struct.unpack('<f', b)[0], 5),)
    if fmt == 6:
        return tuple(round(v / 255, 3) for v in b)
    return b.hex()
