"""Extract per-function PPC disassembly from ReXGlue generated code.

The codegen emits each guest instruction as a `// mnemonic operands` comment inside
`DEFINE_REX_FUNC(sub_XXXXXXXX) {`. This turns a generated directory into a cache:
{addr: [asm lines]}, so other tools can fingerprint, search and diff functions.

usage: python3 -I genasm.py GENERATED_DIR OUT.json.gz
"""
import gzip, json, os, re, sys

FUNC = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)')
ASM = re.compile(r'^\t// (\S.*)$')


def extract(gen_dir):
    funcs = {}
    for name in sorted(os.listdir(gen_dir)):
        if not name.endswith('.cpp') or '_recomp.' not in name:
            continue
        cur = None
        with open(os.path.join(gen_dir, name), errors='replace') as f:
            for line in f:
                m = FUNC.match(line)
                if m:
                    cur = funcs.setdefault(int(m.group(1), 16), [])
                    continue
                if cur is not None:
                    a = ASM.match(line)
                    if a:
                        cur.append(a.group(1))
    return funcs


def load(path):
    with gzip.open(path, 'rt') as f:
        return {int(k, 16): v for k, v in json.load(f).items()}


def main():
    funcs = extract(sys.argv[1])
    with gzip.open(sys.argv[2], 'wt') as f:
        json.dump({f'{k:08X}': v for k, v in funcs.items()}, f)
    print(f'{len(funcs)} functions, {sum(map(len, funcs.values()))} instructions')


if __name__ == '__main__':
    main()
