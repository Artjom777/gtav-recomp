"""Compare per-draw ALU constants of a native frame dump with the emulator's (draws.bin of both).

usage: python3 compare_constants.py NATIVE_DUMP_DIR EMU_DUMP_DIR [max_draws_to_list]
The two draw sequences are aligned on (primitive, count) with a diff (the emulator also sees
D3D-internal draws). For each aligned pair, lists the constant registers that differ.
"""
import difflib, struct, sys, collections


def load(path):
    data = open(path, 'rb').read()
    rec = 4 * 2052  # {prim, count, pixel shader ucode hash (u64), 2048 constants}
    out = []
    for i in range(len(data) // rec):
        prim, count, lo, hi = struct.unpack_from('<4I', data, i * rec)
        consts = struct.unpack_from('<2048f', data, i * rec + 16)
        out.append((prim, count, consts, lo | hi << 32))
    return out


def best_emu_frame(nat, emu_root):
    """The emulated frame (of a dumped window) whose first draws' constants match the native frame."""
    import os
    best = None
    for d in sorted(os.listdir(emu_root)):
        path = f'{emu_root}/{d}/draws.bin'
        if not os.path.exists(path):
            continue
        emu = load(path)
        score = sum(1 for (_, _, a, _), (_, _, b, _) in zip(nat[:40], emu[:40]) for x, y in zip(a, b) if abs(x - y) < 1e-4)
        if best is None or score > best[0]:
            best = (score, d, emu)
    print(f'best matching emulated frame: {best[1]} (score {best[0]})')
    return best[2]


def main():
    nat = load(sys.argv[1] + '/draws.bin')
    emu = best_emu_frame(nat, sys.argv[2])
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    print(f'native draws {len(nat)}, emulated draws {len(emu)}')
    sm = difflib.SequenceMatcher(None, [(p, c, h) for p, c, _, h in nat], [(p, c, h) for p, c, _, h in emu], autojunk=False)
    pairs = [(a.a + k, a.b + k) for a in sm.get_matching_blocks() for k in range(a.size)]
    print(f'aligned {len(pairs)}')
    reg_hist = collections.Counter()
    shown = 0
    for ni, ei in pairs:
        nc, ec = nat[ni][2], emu[ei][2]
        bad = []
        for r in range(512):
            a, b = nc[4 * r:4 * r + 4], ec[4 * r:4 * r + 4]
            if any(abs(x - y) > 1e-4 * max(1, abs(y)) for x, y in zip(a, b)):
                bad.append(r)
        for r in bad:
            reg_hist[r] += 1
        if bad and shown < limit:
            shown += 1
            ex = bad[0]
            print(f'draw n{ni}/e{ei} prim {nat[ni][0]} count {nat[ni][1]}: {len(bad)} regs differ, e.g. '
                  f'{"VS" if ex < 256 else "PS"} c{ex % 256} native {nc[4*ex:4*ex+4]} emu {ec[4*ex:4*ex+4]}')
    print('most often differing registers:', [(('VS' if r < 256 else 'PS') + f'c{r % 256}', n) for r, n in reg_hist.most_common(15)])


if __name__ == '__main__':
    main()
