"""Tree orbs: compare _globalEntityScale (VS c232) of the wind-animated leaf draws (VS AF15EF63)
between a native frame dump and the emulator's dumped frames (shadow mode, same F9).

usage: python3 -I compare_leaf_scale.py NATIVE_DUMP_DIR EMU_DUMP_ROOT

Draws are paired by gWorld translation (c3.xyz): the emulator's draws.bin has no vertex shader hash.
"""
import glob, os, re, struct, sys

REC = 4 * 2052  # {prim, count, ps ucode hash (u64), 1024 VS + 1024 PS float constants}


def records(path):
    data = open(path, 'rb').read()
    return [data[i * REC:(i + 1) * REC] for i in range(len(data) // REC)]


def reg(r, n):
    return tuple(round(x, 3) for x in struct.unpack_from('<4f', r, 16 + n * 16))


def main():
    nd, emu_root = sys.argv[1], sys.argv[2]
    draws = [l for l in open(os.path.join(nd, 'commands.txt')) if l.startswith('draw')]
    nat = records(os.path.join(nd, 'draws.bin'))
    frame = int(re.search(r'(\d+)$', nd.rstrip('/')).group(1))
    emu = {}
    for d in sorted(glob.glob(os.path.join(emu_root, 'frame_*'))):
        n = int(d.rsplit('_', 1)[1])
        if abs(n - frame) <= 40 and os.path.exists(d + '/draws.bin'):
            emu[n] = records(d + '/draws.bin')
    print(f'native frame {frame}, emulator frames {sorted(emu)}')
    for i, l in enumerate(draws):
        if 'vs=AF15EF63' not in l:
            continue
        t = reg(nat[i], 3)[:3]
        print(f'native {i}: T {t} c231 {reg(nat[i], 231)} c232 {reg(nat[i], 232)} c233 {reg(nat[i], 233)}')
        for n, recs in sorted(emu.items()):
            hits = [j for j, r in enumerate(recs) if all(abs(a - b) < 0.01 for a, b in zip(reg(r, 3)[:3], t))]
            for j in hits[:3]:
                r = recs[j]
                print(f'    emu {n} #{j} n={struct.unpack_from("<2I", r)[1]}: c231 {reg(r, 231)} '
                      f'c232 {reg(r, 232)} c233 {reg(r, 233)}')


if __name__ == '__main__':
    main()
