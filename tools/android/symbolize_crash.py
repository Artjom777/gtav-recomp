#!/usr/bin/env python3
"""Map the first native (non-ART) fault in debug_crash.sh's lldb output to module+symbol.
usage: symbolize_crash.py LLDB_OUT MAPS  (uses build/android/libmain.so and the SDK install libs)"""
import os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NDK = os.path.expanduser('~/android-tools/sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/')
LOCAL = {'libmain.so': ROOT + '/build/android/libmain.so',
         'librexruntime.so': ROOT + '/rexglue-sdk/out/install/android-arm64/lib/librexruntime.so',
         'librexgpu-xenos.so': ROOT + '/rexglue-sdk/out/install/android-arm64/lib/librexgpu-xenos.so'}
out, maps_path = open(sys.argv[1]).read(), sys.argv[2]
maps = []
for line in open(maps_path):
    p = line.split()
    if len(p) < 6: continue
    a, b = (int(x, 16) for x in p[0].split('-'))
    maps.append((a, b, int(p[2], 16), p[5]))
def seg_vaddr(path, off):
    r = subprocess.run([NDK + 'llvm-readelf', '-lW', path], capture_output=True, text=True).stdout
    for l in r.splitlines():
        f = l.split()
        if f and f[0] == 'LOAD':
            o, v, sz = int(f[1], 16), int(f[2], 16), int(f[4], 16)
            if o <= off < o + sz: return off - o + v
    return off
blocks = re.split(r'\n(?=\* thread)', out)
for blk in blocks:
    m = re.match(r'\* thread #\d+, name = \'([^\']*)\', stop reason = signal (SIG\w+)', blk)
    if not m or m.group(2) == 'SIGSTOP': continue
    frames = [int(x, 16) for x in re.findall(r'frame #\d+: 0x([0-9a-f]+)', blk)]
    if len(frames) < 2: continue  # the stop banner; the backtrace block follows
    first = next((f for f in frames if f), 0)
    if 0x40000000 <= first < 0x60000000: continue  # ART JIT implicit null checks
    print(f'thread {m.group(1)} {m.group(2)}')
    regs = dict(re.findall(r'\s(pc|lr) = 0x([0-9a-f]+)', blk))
    addrs = frames + [int(v, 16) for v in regs.values()]
    for x in addrs:
        name, off = '?', 0
        for a, b, o, n in maps:
            if a <= x < b: name, off = n, x - a + o; break
        base = os.path.basename(name)
        sym = ''
        if base in LOCAL:
            v = seg_vaddr(LOCAL[base], off)
            sym = subprocess.run([NDK + 'llvm-symbolizer', '--obj=' + LOCAL[base], '-f', '-C', '-i', hex(v)],
                                 capture_output=True, text=True).stdout.split('\n\n')[0].replace('\n', ' | ')
        print(f'  {x:#x} {base}+{off:#x} {sym[:220]}')
    break
