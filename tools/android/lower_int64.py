#!/usr/bin/env python3
"""Lower 64-bit integers in a SPIR-V module to uvec2 (low, high) for GPUs without shaderInt64.

The game's translated shaders (XenosRecomp + dxc) do their buffer-device-address arithmetic in
64-bit integers. Adreno 730 on Vulkan 1.1 has no shaderInt64: its compiler asserts on some of it
and miscompiles the rest (GPU faults). Physical-storage-buffer pointers may be bitcast from a
vector of two 32-bit integers, so every 64-bit value becomes a uvec2 and the few operations the
shaders use (add, sign/zero extend, truncate, multiply, and/or/xor/not, ==/!=, loads) are expanded
into 32-bit ones. 64-bit loads through device addresses are split into two 32-bit loads.

usage: lower_int64.py IN.spv OUT.spv   (needs spirv-dis / spirv-as on PATH)
Exits 0 and copies the input unchanged when the module has no 64-bit integers.
"""
import re
import shutil
import subprocess
import sys


class Lowerer:
    def __init__(self, text):
        self.lines = text.splitlines()
        self.next_id = 1 + max(int(x) for x in re.findall(r'%(\d+)', text))
        self.types = {}      # id -> declaration text (after '=')
        self.result_type = {}  # id -> type id
        self.ptr_target = {}  # pointer type id -> (storage class, pointee type id)
        self.ptr_from_addr = {}  # pointer value id -> address value id (from OpBitcast)
        self.global_add = []  # new global declarations
        self.cache = {}

    def new(self):
        i = self.next_id
        self.next_id += 1
        return '%' + str(i)

    def find_type(self, decl):
        for i, d in self.types.items():
            if d == decl:
                return i
        return None

    def get_type(self, decl):
        key = ('type', decl)
        if key in self.cache:
            return self.cache[key]
        t = self.find_type(decl)
        if t is None:
            t = self.new()
            self.global_add.append('%s = %s' % (t, decl))
            self.types[t] = decl
        self.cache[key] = t
        return t

    def const(self, value):
        key = ('const', value)
        if key not in self.cache:
            c = self.new()
            (self.inline if self.inline is not None else self.global_add).append(
                '%s = OpConstant %s %d' % (c, self.uint, value))
            self.cache[key] = c
        return self.cache[key]

    def run(self):
        # Pass 1: types.
        for l in self.lines:
            m = re.match(r'\s*(%\d+) = (OpType\w+.*)$', l)
            if m:
                self.types[m.group(1)] = m.group(2).strip()
        t64 = [i for i, d in self.types.items() if re.fullmatch(r'OpTypeInt 64 [01]', d)]
        if not t64:
            return None
        self.uint = self.find_type('OpTypeInt 32 0')
        self.int = self.find_type('OpTypeInt 32 1')
        self.bool = self.find_type('OpTypeBool')
        insert_uint = None
        if not self.uint:
            self.uint = insert_uint = self.new()
            self.types[self.uint] = 'OpTypeInt 32 0'
        v2 = self.find_type('OpTypeVector %s 2' % self.uint)
        self.v2 = t64[0]
        rename = {t: self.v2 for t in t64[1:]}
        if v2:
            rename[v2] = self.v2
        for l in self.lines:
            m = re.match(r'\s*(%\d+) = OpTypePointer (\w+) (%\d+)', l)
            if m:
                self.ptr_target[m.group(1)] = (m.group(2), rename.get(m.group(3), m.group(3)))

        out = []
        first_function = None
        self.t64set = set(t64)
        self.ids64 = set()
        self.inline = None
        for l in self.lines:
            orig = re.match(r'\s*(%\d+) = (Op\w+)\s+(%\d+)', l)
            od = re.match(r'\s*(%\d+) = (OpTypeInt|OpTypeVector) ', l)
            orig_dup = bool(od and od.group(1) in rename)
            self.cur64 = bool(orig and orig.group(3) in self.t64set and not orig.group(2).startswith('OpType'))
            if self.cur64:
                self.ids64.add(orig.group(1))
            # Unify the 64-bit types (and any existing uvec2) into one uvec2 id.
            l = re.sub(r'%\d+', lambda m: rename.get(m.group(0), m.group(0)), l)
            s = l.strip()
            if s == 'OpCapability Int64' or orig_dup:
                continue  # orig_dup: a second 64-bit type (or uvec2) renamed onto self.v2
            m = re.match(r'(%\d+) = (Op\w+)\s*(.*)$', s)
            if m and m.group(1) == self.v2 and m.group(2) == 'OpTypeInt':
                if insert_uint:
                    out.append('%s = OpTypeInt 32 0' % insert_uint)
                out.append('%s = OpTypeVector %s 2' % (self.v2, self.uint))
                continue
            if orig_dup:
                continue  # removed duplicate (renamed to self.v2)
            if s.startswith('OpFunction') or (m and m.group(2) == 'OpFunction'):
                if first_function is None:
                    first_function = len(out)
            if m:
                rid, op, rest = m.group(1), m.group(2), m.group(3).split()
                if rest and rest[0].startswith('%') and op not in ('OpTypePointer', 'OpTypeStruct', 'OpTypeVector',
                                                                    'OpTypeArray', 'OpTypeFunction', 'OpTypeRuntimeArray'):
                    self.result_type[rid] = rest[0]
                if op == 'OpConstant' and self.cur64:
                    v = int(rest[1], 0)
                    v &= (1 << 64) - 1
                    self.inline = out  # scalar halves go right here, before their first use
                    lo_c, hi_c = self.const(v & 0xFFFFFFFF), self.const(v >> 32)
                    self.inline = None
                    out.append('%s = OpConstantComposite %s %s %s' % (rid, self.v2, lo_c, hi_c))
                    continue
                if op == 'OpBitcast' and self.ptr_target.get(rest[0], ('', ''))[0] == 'PhysicalStorageBuffer':
                    self.ptr_from_addr[rid] = rest[1]
                expanded = self.expand(rid, op, rest)
                if expanded is not None:
                    out.extend(expanded)
                    continue
            out.append(s)
        if first_function is None:
            raise SystemExit('no function')
        # Globals right before the first function (all their dependencies are declared above).
        out[first_function:first_function] = self.global_add
        return '\n'.join(out) + '\n'

    def is64(self, i):
        return i in self.ids64

    def expand(self, rid, op, rest):
        if not rest or op.startswith('OpType'):
            return None
        rtype = rest[0]
        ops = rest[1:]
        v2, uint = self.v2, self.uint
        r64 = self.cur64
        any64 = r64 or any(self.is64(o) for o in ops if o.startswith('%'))
        if not any64:
            return None
        code = []

        def emit(fmt, *args):
            r = self.new()
            code.append(('%s = ' + fmt) % ((r,) + args))
            return r

        def lo(x):
            return emit('OpCompositeExtract %s %s 0', uint, x)

        def hi(x):
            return emit('OpCompositeExtract %s %s 1', uint, x)

        def add64(a, b):
            alo, blo, ahi, bhi = lo(a), lo(b), hi(a), hi(b)
            s = emit('OpIAdd %s %s %s', uint, alo, blo)
            bool_t = self.bool or self.get_type('OpTypeBool')
            self.bool = bool_t
            c = emit('OpULessThan %s %s %s', bool_t, s, alo)
            ci = emit('OpSelect %s %s %s %s', uint, c, self.const(1), self.const(0))
            h = emit('OpIAdd %s %s %s', uint, ahi, bhi)
            h2 = emit('OpIAdd %s %s %s', uint, h, ci)
            return s, h2

        def to_uint(x):
            t = self.result_type.get(x)
            if t == uint:
                return x
            return emit('OpBitcast %s %s', uint, x)

        if op == 'OpIAdd' and r64:
            s, h = add64(ops[0], ops[1])
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, s, h))
            return code
        if op == 'OpISub' and r64:
            nb = emit('OpNot %s %s', v2, ops[1])
            one = emit('OpCompositeConstruct %s %s %s', v2, self.const(1), self.const(0))
            s1, h1 = add64(nb, one)
            neg = emit('OpCompositeConstruct %s %s %s', v2, s1, h1)
            s, h = add64(ops[0], neg)
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, s, h))
            return code
        if op == 'OpSConvert' and r64:
            x = ops[0]
            if self.result_type.get(x) == uint:
                xi = emit('OpBitcast %s %s', self.int or self.get_type('OpTypeInt 32 1'), x)
            else:
                xi = x
            if not self.int:
                self.int = self.get_type('OpTypeInt 32 1')
            sh = emit('OpShiftRightArithmetic %s %s %s', self.int, xi, self.const(31))
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, to_uint(x), to_uint(sh)))
            return code
        if op == 'OpUConvert' and r64:
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, to_uint(ops[0]), self.const(0)))
            return code
        if op in ('OpUConvert', 'OpSConvert') and self.is64(ops[0]):  # truncate
            l0 = lo(ops[0])
            if rtype == uint:
                code.append('%s = OpCopyObject %s %s' % (rid, uint, l0))
            else:
                code.append('%s = OpBitcast %s %s' % (rid, rtype, l0))
            return code
        if op == 'OpBitcast':
            if r64 and self.is64(ops[0]):
                code.append('%s = OpCopyObject %s %s' % (rid, v2, ops[0]))
                return code
            return None  # pointer <-> uvec2 bitcasts are valid as they are
        if op == 'OpIMul' and r64:
            a, b = ops
            st = self.get_type('OpTypeStruct %s %s' % (uint, uint))
            alo, blo, ahi, bhi = lo(a), lo(b), hi(a), hi(b)
            m = emit('OpUMulExtended %s %s %s', st, alo, blo)
            mlo = emit('OpCompositeExtract %s %s 0', uint, m)
            mhi = emit('OpCompositeExtract %s %s 1', uint, m)
            t1 = emit('OpIMul %s %s %s', uint, alo, bhi)
            t2 = emit('OpIMul %s %s %s', uint, ahi, blo)
            h = emit('OpIAdd %s %s %s', uint, mhi, t1)
            h2 = emit('OpIAdd %s %s %s', uint, h, t2)
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, mlo, h2))
            return code
        if op == 'OpSelect' and r64:
            # A scalar condition selecting between vectors is SPIR-V 1.4+; Adreno's compiler
            # asserts on it ("src expected to have two components"): select per component.
            cond = ops[0]
            bool_t = self.bool or self.get_type('OpTypeBool')
            self.bool = bool_t
            v2b = self.get_type('OpTypeVector %s 2' % bool_t)
            c2 = emit('OpCompositeConstruct %s %s %s', v2b, cond, cond)
            code.append('%s = OpSelect %s %s %s %s' % (rid, v2, c2, ops[1], ops[2]))
            return code
        if op in ('OpBitwiseAnd', 'OpBitwiseOr', 'OpBitwiseXor', 'OpNot', 'OpPhi', 'OpCopyObject',
                  'OpStore', 'OpCompositeConstruct', 'OpCompositeExtract', 'OpCompositeInsert',
                  'OpAccessChain', 'OpVariable', 'OpFunctionParameter', 'OpFunctionCall', 'OpUndef',
                  'OpConstantComposite', 'OpConstantNull'):
            return None  # componentwise on uvec2, or only the type changes
        if op in ('OpIEqual', 'OpINotEqual'):
            bool_t = rtype
            v2b = self.get_type('OpTypeVector %s 2' % bool_t)
            e = emit('%s %s %s %s', op, v2b, ops[0], ops[1])
            code.append('%s = %s %s %s' % (rid, 'OpAll' if op == 'OpIEqual' else 'OpAny', bool_t, e))
            return code
        if op == 'OpLoad' and r64:
            ptr = ops[0]
            addr = self.ptr_from_addr.get(ptr)
            if addr is None:
                return None  # push constants / function variables: a plain uvec2 load
            pu = self.get_type('OpTypePointer PhysicalStorageBuffer %s' % uint)
            for t, (sc, tgt) in self.ptr_target.items():
                if sc == 'PhysicalStorageBuffer' and tgt == uint:
                    pu = t
                    break
            four = emit('OpCompositeConstruct %s %s %s', v2, self.const(4), self.const(0))
            s, h = add64(addr, four)
            addr4 = emit('OpCompositeConstruct %s %s %s', v2, s, h)
            p0 = emit('OpBitcast %s %s', pu, addr)
            p1 = emit('OpBitcast %s %s', pu, addr4)
            w0 = emit('OpLoad %s %s Aligned 4', uint, p0)
            w1 = emit('OpLoad %s %s Aligned 4', uint, p1)
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, v2, w0, w1))
            return code
        if op == 'OpLoad':
            return None
        raise SystemExit('unhandled 64-bit op: %s %s' % (op, ' '.join(rest)))


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = subprocess.run(['spirv-dis', '--raw-id', src], check=True, capture_output=True, text=True).stdout
    out = Lowerer(text).run()
    if out is None:
        if src != dst:
            shutil.copyfile(src, dst)
        return
    tmp = dst + '.s'
    open(tmp, 'w').write(out)
    subprocess.run(['spirv-as', '--preserve-numeric-ids', '--target-env', 'vulkan1.1', tmp, '-o', dst], check=True)
    import os
    os.remove(tmp)


if __name__ == '__main__':
    main()
