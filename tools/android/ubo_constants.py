#!/usr/bin/env python3
"""Rewrite XenosRecomp HLSL (and shader_common.h) so shader constants come from uniform buffers
instead of buffer-device-address loads (Android / Adreno).

On Adreno every vk::RawBufferLoad is a memory fetch per invocation; uniform buffers are loaded
into the shader constant RAM once per draw. Pixel shaders were ~60% of the GPU frame and read
dozens of constants per pixel. The backend binds set 4 (backend.cpp, kConstantSet) with three
dynamic uniform buffers at the same upload-ring offsets it pushes as addresses:
  binding 0: vertex shader ALU constants (256 float4), 1: pixel shader ALU constants,
  binding 2: shared constants (descriptor indices, booleans, ... 1792 bytes = 112 uint4).
The vertex-fetch records (vfetchIndirect: guest buffer addresses) stay device-address loads.

usage: ubo_constants.py FILE...   (rewrites in place; fails on a constant load it cannot map)
"""
import re
import sys

DECLS = """
struct XenosAluConstants { float4 c[256]; };
struct XenosSharedConstants { uint4 u[112]; };
[[vk::binding(0, 4)]] ConstantBuffer<XenosAluConstants> g_VSC;
[[vk::binding(1, 4)]] ConstantBuffer<XenosAluConstants> g_PSC;
[[vk::binding(2, 4)]] ConstantBuffer<XenosSharedConstants> g_SC;
"""

ALU = {'Vertex': 'g_VSC', 'Pixel': 'g_PSC'}
CAST = {'uint': '', 'float': 'asfloat', 'int': 'asint'}


def shared_word(off):
    return 'g_SC.u[%d][%d]' % (off // 16, (off % 16) // 4)


def rewrite(text):
    # ALU constants by byte offset: RawBufferLoad<float4>(X + 3792, 0x10)
    text = re.sub(r'vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ (\d+), 0x10\)',
                  lambda m: '%s.c[%d]' % (ALU[m.group(1)], int(m.group(2)) // 16), text)
    # Indexed ALU constants: RawBufferLoad<float4>(X + (EXPR) * 16, 0x10)
    text = re.sub(r'vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ (\(.*?\)) \* 16, 0x10\)',
                  lambda m: '%s.c[%s]' % (ALU[m.group(1)], m.group(2)), text)
    # Shared scalars: RawBufferLoad<T>(SharedConstants + A [+ B])
    text = re.sub(r'vk::RawBufferLoad<(uint|float|int)>\(g_PushConstants\.SharedConstants \+ (\d+)(?: \+ (\d+))?\)',
                  lambda m: '%s(%s)' % (CAST[m.group(1)], shared_word(int(m.group(2)) + int(m.group(3) or 0))), text)
    # g_ColorScale(INDEX): RawBufferLoad<float4>(SharedConstants + 560 + (INDEX) * 16)
    text = re.sub(r'vk::RawBufferLoad<float4>\(g_PushConstants\.SharedConstants \+ (\d+) \+ \(INDEX\) \* 16\)',
                  lambda m: 'asfloat(g_SC.u[%d + (INDEX)])' % (int(m.group(1)) // 16), text)
    text = text.replace('(uint)(', '(')  # tidy: CAST[uint] is empty
    left = [l for l in text.splitlines()
            if re.search(r'RawBufferLoad<[^>]*>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants', l)
            or re.search(r'RawBufferLoad<[^>]*>\(g_PushConstants\.SharedConstants \+ \d', l)]
    if left:
        raise SystemExit('unmapped constant loads:\n' + '\n'.join(left[:5]))
    anchor = '[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;'
    if anchor in text and 'g_VSC;' not in text:
        text = text.replace(anchor, anchor + '\n' + DECLS, 1)
    return text


for path in sys.argv[1:]:
    src = open(path).read()
    out = rewrite(src)
    if out != src:
        open(path, 'w').write(out)
