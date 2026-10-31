#!/usr/bin/env python3
"""Run lower_int64.py on the SPIR-V embedded in dxc -Fh headers (the native renderer's own
shaders), rewriting each header in place. Headers without 64-bit integers are left alone.
usage: lower_header.py HEADER...   (needs spirv-dis / spirv-as)"""
import os
import re
import subprocess
import sys
import tempfile

LOWER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lower_int64.py')

for path in sys.argv[1:]:
    text = open(path).read()
    m = re.search(r'const unsigned char (\w+)\[\] = \{([^}]*)\};', text)
    if not m or 'OpCapability Int64' not in text:
        continue
    name, body = m.group(1), m.group(2)
    data = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', body))
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, 'in.spv'), os.path.join(tmp, 'out.spv')
        open(src, 'wb').write(data)
        subprocess.run([sys.executable, '-I', LOWER, src, dst], check=True)
        out = open(dst, 'rb').read()
    lines = ['// %s: 64-bit integers lowered to uvec2 (tools/android/lower_int64.py)' % os.path.basename(path),
             'const unsigned char %s[] = {' % name]
    for i in range(0, len(out), 12):
        lines.append('  ' + ', '.join('0x%02x' % b for b in out[i:i + 12]) + ',')
    lines.append('};')
    open(path, 'w').write('\n'.join(lines) + '\n')
    print('lowered', path)
