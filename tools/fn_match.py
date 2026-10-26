"""Map functions between two Xbox 360 guest images (e.g. base v9 -> TU27).

usage: python3 -I fnmatch.py BASE_IMG NEW_IMG ADDR [ADDR...]
Images are guest memory dumps starting at 0x82000000. Each instruction is masked so
build-dependent fields (branch displacements, address/offset immediates) are ignored,
then the first N masked words of the function are searched for in the new image.
"""
import struct, sys

BASE = 0x82000000


def masked(w):
    op = w >> 26
    if op == 18:            # b/bl: displacement
        return w & 0xFC000003
    if op == 16:            # bc: displacement
        return w & 0xFFFF0003
    if op in (14, 15, 24, 25, 28, 29) or 32 <= op <= 55 or op in (58, 62):
        return w & 0xFFFF0000  # addi/addis/ori/oris/andi/loads/stores: immediate
    return w


def words(img):
    return struct.unpack('>%dI' % (len(img) // 4), img[:len(img) // 4 * 4])


def main():
    a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
    wa = words(a); wb = [masked(x) for x in words(b)]
    index = {}
    for i in range(len(wb) - 3):
        index.setdefault(tuple(wb[i:i + 4]), []).append(i)
    for arg in sys.argv[3:]:
        addr = int(arg, 16); i0 = (addr - BASE) // 4
        for n in (48, 32, 24, 16, 12):
            pat = [masked(x) for x in wa[i0:i0 + n]]
            hits = [j for j in index.get(tuple(pat[:4]), []) if wb[j:j + n] == pat]
            if hits:
                print(f'{addr:08X} -> ' + ' '.join(f'{BASE + 4 * j:08X}' for j in hits[:5]) +
                      f'  (n={n}{", ambiguous" if len(hits) > 1 else ""})')
                break
        else:
            print(f'{addr:08X} -> no match')


main()
