"""Numeric comparison of a native frame dump's raw resolves with the emulated renderer's.

usage: python compare_raw.py NATIVE_DUMP_DIR EMU_DUMP_ROOT [emu_frame_dir]
For every native <slot>_<addr>_f<fmt>_<w>x<h>_r<row>.raw the k-th native resolve of an address is
paired with the k-th emulated one (emulated files are <seq>_<addr>.bin in write order), a 32x18
grid of texels is decoded on both sides, and the mean absolute difference per channel is printed,
overall and for the top / middle / bottom thirds (to spot band-shaped errors).
The emulated frame is chosen by compare_dump.pick_emu_frame (draw constants) unless given.
"""
import os, re, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_dump import half, pick_emu_frame, tiled_offset

# Plume RenderFormat values (plume_render_interface_types.h) -> numpy decode
NATIVE = {10: ('<f2', 4), 11: ('<u2', 4), 20: ('u1', 4), 24: ('u1', 4), 26: ('<f2', 2), 29: ('<i2', 2),
          34: ('<f4', 1), 2: ('<f4', 4), 16: ('<f4', 2)}
SCALE = {11: 65535.0, 20: 255.0, 24: 255.0, 29: 32767.0}


def native_texels(path, fmt, w, h, row, pts):
    dt, ch = NATIVE[fmt]
    a = np.fromfile(path, dtype=dt).astype(np.float64).reshape(h, row, ch)
    if fmt in SCALE:
        a = a / SCALE[fmt]
    if fmt == 24:  # BGRA -> RGBA
        a = a[..., [2, 1, 0, 3]]
    return np.array([a[y, x] for x, y in pts])


def extent(words):
    """Guest layout of a resolve destination: (pitch, height, bpp, fmt, endian, tiled, size)."""
    fmt, endian = words[1] & 0x3F, (words[1] >> 6) & 3
    tiled = (words[0] >> 31) & 1
    w, h = (words[2] & 0x1FFF) + 1, ((words[2] >> 13) & 0x1FFF) + 1
    pitch = max(((words[0] >> 22) & 0x1FF) * 32, w)
    bpp = {2: 1, 30: 2, 6: 4, 7: 4, 54: 4, 25: 4, 31: 4, 36: 4, 22: 4, 23: 4, 29: 8, 32: 8, 26: 8}.get(fmt)
    size = pitch * ((h + 31) & ~31) * (bpp or 0)
    return pitch, h, bpp, fmt, endian, tiled, size


def texel(buf, words, x, y):
    pitch, _, bpp, fmt, endian, tiled, _ = extent(words)
    off = tiled_offset(x, y, pitch, bpp.bit_length() - 1) if tiled else (y * pitch + x) * bpp
    b = bytes(buf[off:off + bpp])
    if endian == 1:
        b = bytes(b[i ^ 1] for i in range(bpp))
    elif endian == 2:
        b = b''.join(b[i:i + 4][::-1] for i in range(0, bpp, 4))
    if fmt in (29, 32):
        return [half(v) for v in struct.unpack('<4H', b)]
    if fmt in (30, 31):
        return [half(v) for v in struct.unpack('<%dH' % (bpp // 2), b)]
    if fmt == 36:
        return [struct.unpack('<f', b)[0]]
    if fmt == 6:  # 0xAARRGGBB little-endian -> B G R A
        return [b[2] / 255, b[1] / 255, b[0] / 255, b[3] / 255]
    if fmt == 54:  # 2_10_10_10 fixed: X bits 0-9, Y 10-19, Z 20-29, W 30-31
        v = struct.unpack('<I', b)[0]
        return [(v & 1023) / 1023, ((v >> 10) & 1023) / 1023, ((v >> 20) & 1023) / 1023, (v >> 30) / 3]
    return None


def emu_image(ed, files, k, addr, words):
    """Guest bytes of the k-th emulated resolve to `addr`, plus the pieces written right after it
    inside the same surface (the game resolves full-screen targets in halves)."""
    a = int(addr, 16)
    size = extent(words)[6]
    if not size:
        return None
    hits = [i for i, (seq, ad) in enumerate(files) if ad == a]
    if k >= len(hits):
        return None
    buf = bytearray(size)
    end = hits[k + 1] if k + 1 < len(hits) else len(files)
    for i in range(hits[k], end):  # other surfaces' resolves interleave with the pieces
        seq, ad = files[i]
        if i != hits[k] and not (a < ad < a + size):
            continue
        data = open(f'{ed}/{seq:03}_{ad:08X}.bin', 'rb').read()
        o = ad - a
        buf[o:o + len(data)] = data[:max(0, size - o)]
    return buf


def best_frame(nd, root):
    """Emulated frame (near the native frame number) whose draws' vertex constants match best:
    draws aligned on (primitive, count), registers c0-c15 compared, first 300 draws."""
    import difflib
    from compare_constants import load
    nat = load(f'{nd}/draws.bin')[:300]
    n = int(re.search(r'frame_(\d+)', nd).group(1))
    best = None
    for d in os.listdir(root):
        m = re.match(r'frame_(\d+)$', d)
        if not m or abs(int(m.group(1)) - n) > 40 or not os.path.exists(f'{root}/{d}/draws.bin'):
            continue
        emu = load(f'{root}/{d}/draws.bin')[:400]
        sm = difflib.SequenceMatcher(None, [(x[0], x[1]) for x in nat], [(x[0], x[1]) for x in emu], autojunk=False)
        score = 0
        for blk in sm.get_matching_blocks():
            for k in range(blk.size):
                a, b = nat[blk.a + k][2], emu[blk.b + k][2]
                score += sum(1 for i in range(64) if abs(a[i] - b[i]) <= 1e-4 * max(1.0, abs(b[i])))
        if best is None or score > best[0]:
            best = (score, d)
    print(f'best emulated frame {best[1]} (score {best[0]})')
    return os.path.join(root, best[1])


def main():
    nd, root = sys.argv[1], sys.argv[2]
    if len(sys.argv) > 3:
        ed = sys.argv[3]
    else:
        ed = best_frame(nd, root)
    print('emulated frame:', ed)
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
    # Resolve pieces: a surface resolved in parts (halves at "at 0,384", cube faces F1-5, mips L1+)
    # is one emulated group; compare the native snapshot taken after its last piece.
    pieces = {}
    for line in open(f'{nd}/commands.txt'):
        m = re.match(r'resolve #(\d+) .*-> ([0-9A-F]{8}) L(\d+) F(\d+) .* at (\d+),(\d+)', line)
        if m:
            pieces[int(m.group(1))] = (m.group(2), int(m.group(3)) > 0 or int(m.group(4)) > 0 or int(m.group(6)) > 0)
    group, last_of_group = {}, {}
    count = {}
    for slot in sorted(pieces):
        addr, cont = pieces[slot]
        if not cont or addr not in count:
            count[addr] = count.get(addr, -1) + 1
        group[slot] = count[addr]
        last_of_group[(addr, count[addr])] = slot
    for f in sorted(f for f in os.listdir(nd) if f.endswith('.raw')):
        m = re.match(r'(\d+)_([0-9A-F]{8})_f(\d+)_(\d+)x(\d+)_r(\d+)\.raw$', f)
        slot, addr, fmt, w, h, row = int(m.group(1)), m.group(2), *map(int, m.groups()[2:])
        if slot not in group or last_of_group[(addr, group[slot])] != slot:
            continue
        k = group[slot]
        if fmt not in NATIVE:
            continue
        buf = emu_image(ed, files, k, addr, words[slot])
        if buf is None or extent(words[slot])[2] is None:
            print(f'{slot:03} {addr} f{fmt} {w}x{h}: no emulated counterpart')
            continue
        gw, gh = 32, 18
        pts = [(min(w - 1, int((i + 0.5) * w / gw)), min(h - 1, int((j + 0.5) * h / gh))) for j in range(gh)
               for i in range(gw)]
        nat = native_texels(f'{nd}/{f}', fmt, w, h, row, pts)
        ev = [texel(buf, words[slot], x, y) for x, y in pts]
        if any(v is None for v in ev):
            print(f'{slot:03} {addr} f{fmt} {w}x{h}: undecodable emulated format')
            continue
        emu = np.array(ev, dtype=np.float64)
        n = min(nat.shape[1], emu.shape[1])
        nat, emu = nat[:, :n], emu[:, :n]
        ok = np.isfinite(nat).all(1) & np.isfinite(emu).all(1) & (np.abs(emu) < 6e4).all(1)
        d = np.abs(nat - emu)
        ys = np.array([y for _, y in pts])
        def band(lo, hi):
            sel = ok & (ys >= lo * h) & (ys < hi * h)
            return d[sel].mean(0) if sel.any() else np.zeros(n)
        mag = np.abs(emu[ok]).mean(0) if ok.any() else np.zeros(n)
        tot = d[ok].mean(0) if ok.any() else np.zeros(n)
        flag = ' <<' if (tot > 0.05 * np.maximum(mag, 0.05)).any() else ''
        f3 = lambda v: '(' + ' '.join(f'{x:.3g}' for x in v) + ')'
        print(f'{slot:03} {addr} f{fmt:<2} {w}x{h}: diff {f3(tot)} of {f3(mag)}  top {f3(band(0, 1/3))} '
              f'mid {f3(band(1/3, 2/3))} bot {f3(band(2/3, 1))}{flag}')


if __name__ == '__main__':
    main()
