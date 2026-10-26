"""Numeric comparison of resolves: native centre texels (small_values.txt) vs the emulator's.

usage: python3 compare_values.py NATIVE_DUMP_DIR EMU_DUMP_ROOT
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_dump import emu_texel, pick_emu_frame


def main():
    nd, root = sys.argv[1], sys.argv[2]
    ed = pick_emu_frame(nd, root)
    index = {}
    for line in open(f'{nd}/index.txt'):
        m = re.match(r'(\d+) addr=(\w+) .* words=([\w,]+)', line)
        if m:
            index[int(m.group(1))] = (m.group(2), [int(x, 16) for x in m.group(3).split(',')])
    emu_files = sorted(f for f in os.listdir(ed) if f.endswith('.bin') and f != 'draws.bin')
    by_addr = {}
    for k, f in enumerate(emu_files):
        by_addr.setdefault(f.split('_')[1][:-4], []).append((k, f))
    for line in open(f'{nd}/small_values.txt'):
        if ' src at ' in line or ' pack at ' in line:
            print('    ' + line.strip())
            continue
        m = re.match(r'(\d+) at (\d+),(\d+) fmt (\d+):(.*)', line)
        i, x, y = int(m.group(1)), int(m.group(2)), int(m.group(3))
        addr, words = index.get(i, (None, None))
        cands = by_addr.get(addr, [])
        emu = min(cands, key=lambda c: abs(c[0] - i))[1] if cands else None
        ev = emu_texel(f'{ed}/{emu}', words, x, y) if emu else None
        print(f'{i:03} {addr} native{m.group(5)}   emu {ev}')


if __name__ == '__main__':
    main()
