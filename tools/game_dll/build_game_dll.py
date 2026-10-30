"""Build gtav_game.dll from ReXGlue codegen output, the way the installer does on a player's PC.

Developer-side twin of the installer's build step (loader/src/build_game.cpp): compiles every
generated source with the freestanding prefix header (gtav/fs/gtav_pch.h) and links a DLL with no
C runtime and no Microsoft libraries.

Linux: compiles position-independent code and links gtav_game.so with ld.lld (version script
gtav_game.exports, -Bsymbolic so calls between game functions stay direct); the hooks, the bridge,
the kernel imports and log2f are left undefined for the dynamic loader.

usage: build_game_dll.py LLVM_BIN GENERATED_DIR INTERFACE_DIR OUT_DIR [-j N]
  GENERATED_DIR  codegen output (gtav_recomp.*.cpp, gtav_init.cpp, gtav_funcs*.h); its gtav_pch.h
                 is replaced by gtav/fs/gtav_pch.h, so pass a copy, not the dev tree
  INTERFACE_DIR  gtav_hooks.h, gtav_game.def, gtav_fast.def, rexruntime.def, ucrtbase.def
                 (Linux: gtav_hooks.h, gtav_game.exports)
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FS = os.path.join(ROOT, "gtav", "fs")

CXXFLAGS = ["--target=x86_64-pc-windows-msvc", "-nostdinc++", "-nostdlibinc", "-O2", "-std=gnu++23",
            "-march=x86-64-v3", "-msse4.1", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
            "-fno-math-errno", "-w"]
# Keep in sync with loader/src/install.cpp (Compile, Link).
CXXFLAGS_LINUX = ["--target=x86_64-linux-gnu", "-nostdinc++", "-nostdlibinc", "-fPIC",
                  "-fno-semantic-interposition", "-fno-stack-protector", "-fcf-protection=none", "-O2",
                  "-std=gnu++23", "-march=x86-64-v3", "-msse4.1", "-fno-exceptions", "-fno-rtti",
                  "-fno-threadsafe-statics", "-fno-math-errno", "-w"]
LDFLAGS_LINUX = ["-shared", "-soname", "gtav_game.so", "-Bsymbolic", "-z", "now", "-z", "relro",
                 "-z", "noexecstack", "--hash-style=gnu", "-O1"]
LINUX = sys.platform.startswith("linux")


def find_tool(llvm_bin, names):
    for n in names:
        p = os.path.join(llvm_bin, n)
        if os.path.isfile(p):
            return p
    sys.exit(f"none of {names} in {llvm_bin}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("llvm_bin")
    ap.add_argument("generated")
    ap.add_argument("interface")
    ap.add_argument("out")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()

    if LINUX:
        clang = find_tool(a.llvm_bin, ["clang++", "clang++-20", "clang++-19"])
        lld = find_tool(a.llvm_bin, ["ld.lld", "ld.lld-20", "ld.lld-19"])
        cxxflags, ext = CXXFLAGS_LINUX, ".o"
    else:
        clang = os.path.join(a.llvm_bin, "clang++.exe")
        lld = os.path.join(a.llvm_bin, "lld-link.exe")
        cxxflags, ext = CXXFLAGS, ".obj"
    obj_dir = os.path.join(a.out, "obj")
    os.makedirs(obj_dir, exist_ok=True)

    shutil.copy(os.path.join(FS, "gtav_pch.h"), os.path.join(a.generated, "gtav_pch.h"))
    shutil.copy(os.path.join(FS, "layout_check.h"), os.path.join(a.generated, "layout_check.h"))
    shutil.copy(os.path.join(a.interface, "gtav_hooks.h"), os.path.join(a.generated, "gtav_hooks.h"))

    sources = sorted(os.path.join(a.generated, f) for f in os.listdir(a.generated)
                     if f.startswith("gtav_recomp.") and f.endswith(".cpp"))
    sources += [os.path.join(a.generated, "gtav_init.cpp"), os.path.join(FS, "game_dll.cpp"),
                os.path.join(FS, "fs_crt.cpp")]
    inc = ["-isystem", os.path.join(FS, "include"), "-I", a.generated]

    def compile_one(src):
        obj = os.path.join(obj_dir, os.path.basename(src) + ext)
        if os.path.exists(obj) and os.path.getmtime(obj) > os.path.getmtime(src):
            return obj, 0.0, ""
        t = time.time()
        r = subprocess.run([clang] + cxxflags + inc + ["-c", src, "-o", obj], capture_output=True,
                           text=True)
        return (obj if r.returncode == 0 else None), time.time() - t, r.stderr[-2000:]

    t0 = time.time()
    objs, done = [], 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for obj, secs, err in ex.map(compile_one, sources):
            done += 1
            if obj is None:
                sys.exit(f"compile failed:\n{err}")
            objs.append(obj)
            if done % 25 == 0 or done == len(sources):
                print(f"compiled {done}/{len(sources)} ({time.time() - t0:.0f} s)", flush=True)

    if LINUX:
        rsp = os.path.join(a.out, "link.rsp")
        with open(rsp, "w") as f:
            f.write("\n".join(f'"{o}"' for o in objs))
        t = time.time()
        subprocess.run([lld] + LDFLAGS_LINUX +
                       [f"--version-script={os.path.join(a.interface, 'gtav_game.exports')}",
                        "-o", os.path.join(a.out, "gtav_game.so"), f"@{rsp}"], check=True)
        print(f"linked gtav_game.so in {time.time() - t:.0f} s, total {time.time() - t0:.0f} s")
        return

    libs = []
    for name, dll in (("gtav_fast", "gtav_fast.exe"), ("rexruntime", None), ("ucrtbase", None)):
        lib = os.path.join(a.out, name + ".lib")
        subprocess.run([lld, "/lib", f"/def:{os.path.join(a.interface, name + '.def')}",
                        f"/out:{lib}", "/machine:x64"], check=True, capture_output=True)
        libs.append(lib)

    rsp = os.path.join(a.out, "link.rsp")
    with open(rsp, "w") as f:
        f.write("\n".join(f'"{o}"' for o in objs + libs))
    t = time.time()
    subprocess.run([lld, "/dll", "/noentry", "/nodefaultlib", "/machine:x64", "/debug:none",
                    f"/def:{os.path.join(a.interface, 'gtav_game.def')}",
                    f"/out:{os.path.join(a.out, 'gtav_game.dll')}", f"@{rsp}"], check=True)
    print(f"linked gtav_game.dll in {time.time() - t:.0f} s, total {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
