"""Sampling profiler for a running gtav_fast.exe on Windows (no perf there).

usage: python sample.py [SECS] [TOP_THREADS] [HZ]
Picks the TOP_THREADS busiest threads (CPU over 2 s), suspends each one HZ times a second, reads
RIP, then symbolizes the hottest addresses with llvm-symbolizer (gtav_fast.pdb / SDK PDBs).
Prints, per thread, the top functions by sample share.
"""
import collections
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 20
TOP = int(sys.argv[2]) if len(sys.argv) > 2 else 4
HZ = float(sys.argv[3]) if len(sys.argv) > 3 else 500
SYMBOLIZER = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
TH32CS_SNAPTHREAD = 4
THREAD_ALL = 0x0002 | 0x0008 | 0x0040 | 0x0010  # suspend/resume, get context, query info
PROCESS_QUERY = 0x0400 | 0x0010


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


CONTEXT_SIZE = 1232
RIP_OFFSET = 0xF8
CONTEXT_CONTROL = 0x100001
ctx_buf = (ctypes.c_byte * (CONTEXT_SIZE + 16))()
ctx_addr = (ctypes.addressof(ctx_buf) + 15) & ~15
ctypes.c_uint32.from_address(ctx_addr + 0x30).value = CONTEXT_CONTROL  # ContextFlags


def find_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gtav_fast.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.replace('"', "").split(",")
        if len(parts) > 1 and parts[0].lower() == "gtav_fast.exe":
            return int(parts[1])
    sys.exit("gtav_fast.exe not running")


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    tids = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            tids.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return tids


def cpu_ms(h):
    c, e, kt, ut = (wt.FILETIME() for _ in range(4))
    if not k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)):
        return 0
    f = lambda t: (t.dwHighDateTime << 32 | t.dwLowDateTime) / 1e4
    return f(kt) + f(ut)


def name_of(h):
    p = ctypes.c_wchar_p()
    k32.GetThreadDescription.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
    if k32.GetThreadDescription(h, ctypes.byref(p)) >= 0 and p.value:
        return p.value
    return ""


def modules(pid):
    hp = k32.OpenProcess(PROCESS_QUERY, False, pid)
    arr = (wt.HMODULE * 1024)()
    need = wt.DWORD()
    psapi.EnumProcessModules(hp, arr, ctypes.sizeof(arr), ctypes.byref(need))
    mods = []
    for i in range(need.value // ctypes.sizeof(wt.HMODULE)):
        name = ctypes.create_unicode_buffer(520)
        psapi.GetModuleFileNameExW(hp, ctypes.c_void_p(arr[i]), name, 520)

        class MI(ctypes.Structure):
            _fields_ = [("base", ctypes.c_void_p), ("size", wt.DWORD), ("entry", ctypes.c_void_p)]
        mi = MI()
        psapi.GetModuleInformation(hp, ctypes.c_void_p(arr[i]), ctypes.byref(mi), ctypes.sizeof(mi))
        mods.append((mi.base or 0, mi.size, name.value))
    return mods


_exports = {}


def nearest_export(module, rva):
    """Name of the closest export at or below rva (system DLLs have no PDB here)."""
    if module not in _exports:
        readobj = os.path.join(os.path.dirname(SYMBOLIZER), "llvm-readobj.exe")
        out = subprocess.run([readobj, "--coff-exports", module], capture_output=True, text=True).stdout
        names, rvas = [], []
        name = None
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("Name:"):
                name = line[5:].strip()
            elif line.startswith("RVA:") and name:
                try:
                    rvas.append(int(line[4:].strip(), 16))
                    names.append(name)
                except ValueError:
                    pass
                name = None
        _exports[module] = sorted(zip(rvas, names))
    best = "?"
    for r, n in _exports[module]:
        if r > rva:
            break
        best = n + "~"  # nearest export at or below: the sample may sit in an internal function after it
    return best


def main():
    pid = find_pid()
    k32.OpenThread.restype = wt.HANDLE
    hs = {t: k32.OpenThread(THREAD_ALL, False, t) for t in threads(pid)}
    hs = {t: h for t, h in hs.items() if h}
    c0 = {t: cpu_ms(h) for t, h in hs.items()}
    time.sleep(2)
    busy = sorted(hs, key=lambda t: cpu_ms(hs[t]) - c0[t], reverse=True)[:TOP]
    share = {t: (cpu_ms(hs[t]) - c0[t]) / 20 for t in busy}
    samples = {t: collections.Counter() for t in busy}
    end = time.perf_counter() + SECS
    period = 1 / HZ
    n = 0
    while time.perf_counter() < end:
        for t in busy:
            h = hs[t]
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            if k32.GetThreadContext(h, ctypes.c_void_p(ctx_addr)):
                samples[t][ctypes.c_uint64.from_address(ctx_addr + RIP_OFFSET).value] += 1
            k32.ResumeThread(h)
        n += 1
        time.sleep(period)
    mods = modules(pid)

    def mod_of(a):
        for base, size, name in mods:
            if base <= a < base + size:
                return base, name
        return None, None

    # Symbolize per module (offset form: 0x<rva> with --adjust-vma = preferred base is not needed for PE).
    todo = collections.defaultdict(set)
    for t in busy:
        for a, _ in samples[t].most_common(4000):
            base, name = mod_of(a)
            if name:
                todo[name].add(a - base)
    sym = {}
    for name, offs in todo.items():
        offs = sorted(offs)
        inp = "\n".join(hex(o) for o in offs)
        r = subprocess.run([SYMBOLIZER, "--obj=" + name, "--relative-address", "--no-inlines", "--functions=linkage",
                            "--output-style=JSON"], input=inp, capture_output=True, text=True)
        import json
        lines = r.stdout.splitlines()
        if len(lines) != len(offs):  # no PDB: one error line for the whole batch
            lines = [""] * len(offs)
        for o, line in zip(offs, lines):
            try:
                fn = json.loads(line)["Symbol"][0]["FunctionName"] or "?"
            except Exception:
                fn = "?"
            if fn in ("?", "<invalid>"):
                fn = nearest_export(name, o)
            sym[(name, o)] = os.path.basename(name) + "!" + fn
    print(f"pid {pid}: {n} sample rounds over {SECS:.0f} s")
    for t in busy:
        total = sum(samples[t].values()) or 1
        fn = collections.Counter()
        for a, c in samples[t].items():
            base, name = mod_of(a)
            if not name:
                fn["?!?"] += c
                continue
            s = sym.get((name, a - base), "?")
            if s.endswith("!?") or s == "?":  # no symbol: bucket by 64 KB of RVA
                s = f"{os.path.basename(name)}!?+{(a - base) >> 16 << 16:X}"
            fn[s] += c
        print(f"== t{t} {name_of(hs[t])!r} ~{share[t]:.0f}% of a core, {total} samples")
        for f, c in fn.most_common(18):
            print(f"  {100 * c / total:5.1f}%  {f[:150]}")


main()
