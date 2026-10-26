"""Find the most similar functions in another image (fuzzy, survives XDK version drift).

usage: python3 -I fnsim.py SRC_ASM.json.gz DST_ASM.json.gz ADDR[=name] [...] [--lo X --hi Y --top N]

Instructions are normalized to mnemonic + registers (immediates and offsets dropped), then
candidates are prefiltered by length and mnemonic histogram and ranked with difflib.
"""
import collections, difflib, math, re, sys
from genasm import load

NUM = re.compile(r'-?(0x)?[0-9A-Fa-f]+\b')


def norm(line):
    op, _, rest = line.partition(' ')
    regs = re.findall(r'\b(?:r|f|v|cr)\d+\b', rest)
    return op + ' ' + ','.join(regs)


def hist(seq):
    return collections.Counter(s.split(' ', 1)[0] for s in seq)


def cos(a, b):
    dot = sum(a[k] * b.get(k, 0) for k in a)
    return dot / (math.sqrt(sum(v * v for v in a.values())) * math.sqrt(sum(v * v for v in b.values())) or 1)


def main():
    args = sys.argv[1:]
    opts = {'--lo': '0', '--hi': 'FFFFFFFF', '--top': '5'}
    for k in list(opts):
        if k in args:
            i = args.index(k); opts[k] = args[i + 1]; del args[i:i + 2]
    lo, hi, top = int(opts['--lo'], 16), int(opts['--hi'], 16), int(opts['--top'])
    src, dst = load(args[0]), load(args[1])
    dn = {a: [norm(x) for x in v] for a, v in dst.items() if lo <= a <= hi and v}
    dh = {a: hist(v) for a, v in dn.items()}
    for spec in args[2:]:
        addr, _, name = spec.partition('=')
        s = [norm(x) for x in src[int(addr, 16)]]
        sh = hist(s)
        cands = [(cos(sh, dh[a]), a) for a, v in dn.items() if 0.6 * len(s) <= len(v) <= 1.6 * len(s) + 4]
        cands = sorted(cands, reverse=True)[:60]
        ranked = sorted(((difflib.SequenceMatcher(None, s, dn[a], autojunk=False).ratio(), a) for _, a in cands), reverse=True)
        print(f'{addr} {name} ({len(s)} insns): ' + '  '.join(f'{a:08X}:{r:.2f}/{len(dn[a])}' for r, a in ranked[:top]))


if __name__ == '__main__':
    main()
