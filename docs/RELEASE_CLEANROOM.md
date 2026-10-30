# Clean-room releases (Windows done, Linux next)

Written 2026-10-29 after the first Windows release (tag `v0.1.0-windows`). This file says how the
Windows release is built and checked, and what the Linux session needs to do to ship the same
way. Read all of it before building a Linux release.

## The rule

A release holds **no Rockstar Games or Microsoft game code or data**: no XEX/XEXP, no disc, TU or
RPF files, no extracted assets, no generated C++, no compiled game code (no monolithic exe with
the recompiled game linked in), no game shaders in any form (binary, HLSL, SPIR-V), no dumps,
traces or logs. The player brings their own Disc 1, Disc 2 and Title Update 27, and everything
game-derived is made on their machine. Never use the leaked Rockstar source, not even for names.

What may ship: our code (GPL-3.0-or-later, `LICENSE`), the ReXGlue SDK runtime and codegen tool
(BSD, patched: `rexglue-sdk-gtav-linux.patch`, which is also exactly what the Windows binaries
were built from), XenosRecomp (MIT, `xenosrecomp-gtav.patch`), dxc (NCSA), LLVM clang + lld
(Apache-2.0 with LLVM exception), our manifest (function addresses and sizes = our analysis), the
codegen partition seed (addresses), the hook list, and `tools/release/expected.txt` (file sizes and
SHA-256 hashes only).

## How the Windows release works

Player side (`loader/`, `gtav_launcher.exe`):
1. Preflight: AVX2, VC++ runtime DLLs present, bundled tools present.
2. Inputs: Disc 1 and Disc 2 as ISO or folder (XDVDFS reader in `loader/src/sources.cpp`), TU27 as
   the STFS file `tu00000009_00000000` or a folder (prefers `disc002/`). Disc swap is detected.
3. Copy with hashing: Disc 2 files (minus `$SystemUpdate`) to `data/game`, TU `default.xexp` to
   `data/game/default.xexp`, `update.rpf` to `data/update`, the four Disc 1 install packages
   (STFS) unpacked to `data/user/0000000000000000/545408A7/00000002/545408A70000000N/`.
   Every file is checked against `expected.txt` (xex/xexp mismatch = hard stop, others ask).
   Resumable through `data/install_state.txt`.
4. Codegen in `data/build`: the shipped `gtav_manifest.toml` + seeded `codegen.partition.json`
   (so every machine splits functions into the same 407 files), `rexgluerd codegen`.
5. The 811 generated files are hashed against the `gen` lines of `expected.txt`.
   **Codegen is byte-deterministic**: a fresh run matched the reference tree exactly.
6. Compile 407 TUs with the bundled clang, freestanding (`gtav/fs/`: our `gtav_pch.h` replaces the
   generated one, `fs_crt.cpp` gives memcpy/memset/memcmp, `include/` shims stdlib.h/malloc.h;
   flags in `loader/src/install.cpp` Compile()). No MSVC STL/CRT or Windows SDK header is used,
   because those are not redistributable. ~200 s on 12 threads.
7. Link `gtav_game.dll` with lld-link (`/noentry /nodefaultlib`) against import libs made from
   the shipped `.def` files; delete `data/build`; write `data/installed.txt`.
8. Play: `gtav_fast.exe` (no game code inside, built with `-DGTAV_GAME_DLL=ON`) loads
   `gtav_game.dll`. Shaders: `data/spirv` starts empty; `shader_compiler.cpp` runs XenosRecomp +
   dxc on each game shader as the game creates it (3618 on a first run, a few hundred ms per
   burst, persisted). The sun-tile fix is a *variant* made there from the player's own shader
   (`kVariants`: one-line replace), not a shipped shader.

Exe/DLL interface (`tools/game_dll/gen_interface.py`, run as a PRE_LINK step of the exe):
hooked guest functions (2930) are defined in the exe and imported by the DLL; the DLL exports the
recompiled functions the exe calls; 9 `gtavfs_*` bridge functions (`src/game_bridge.cpp`) give the
DLL MMIO, timebase, indirect-call lookup, the global lock, traps. `gtav/fs/layout_check.h` pins
the PPCContext / PPCImageInfo layout on both sides with static_asserts.

Developer side:
- `build_release_exe.cmd` (exe, tree `build-dll`), `build_launcher.cmd`.
- `tools/game_dll/build_game_dll.py` = the installer's compile + link, for testing.
- `tools/game_dll/make_expected.py data ...` + `gen ...` → `tools/release/expected.txt`.
- `tools/release/make_release.py OUT --llvm DIR --expected tools/release/expected.txt --licenses DIR --zip`
  (refuses forbidden extensions; collects every third-party license; LLVM 19.1.5 from the
  official installer: clang.exe → `tools/llvm/bin/clang++.exe`, lld-link.exe, `lib/clang/19/include`).
- `tools/release/scan_release.cmd` → `taint_scan.py`: content-defined 32-byte windows of the
  release vs the decrypted executable image, the TU xexp, update.rpf, every dumped/translated/
  compiled game shader and the generated C++. Windows also found in a control set (system
  libraries, SDK sources and codegen templates, computed CRC/AES tables) are generic; padding,
  counting tables and the SPIR-V preamble are ignored; any common run of ≥128 content bytes
  fails. The Windows release: **PASS, 0 failing** (longest remaining runs: codegen instruction
  templates 112 B, SPIR-V boilerplate 32 B).
- End-to-end test: `gtav_launcher.exe --unattended` reads the three input paths from
  `data/launcher.ini` (one per line), logs to `data/logs/unattended.txt`, answers no to every
  question. Run it on a copy of the release folder, then boot the result hidden with the
  launcher's arguments. A fresh install has no profile or save: the game waits on its first-boot
  screens until A is pressed (feed A presses with `--input_script`), it is not a hang.

## History scrub (done 2026-10-29) — Linux must re-clone

`gtav/src/native/shaders/ps_D258EBE32001470C_tiles.hlsl` was XenosRecomp's translation of a game
shader. It was removed from the tree and from **all history** with git filter-repo, and
`windows-native` was force-pushed. Every commit from `214b060` on has a new hash. Then the old
Linux `main` was merged in (`main` and `windows-native` had separate roots) and **everything now
lives on `main`**; `windows-native` points at the same commit and is no longer needed.

- Any clone made before the scrub (the Linux machine, `E:/gtav-rex/linux-native-*`, `.codex`
  worktrees) still has the old commits. **Do not push from it, and do not merge or cherry-pick
  old-hash commits.** Re-clone `main`, or save local work as a patch and
  `git fetch && git reset --hard origin/main`, then check with
  `git log --all -- '*ps_D258EBE32001470C*'` (must print nothing).
- Text is stored with LF (`.gitattributes`; `.cmd` files CRLF). The old `main` had CRLF in a few
  files; the merge took the LF versions.
- If the Linux tree has its own copy of that file or any other translated game shader
  (`ps_*.hlsl`, `vs_*.hlsl`, `*.spv` from the game, `shader_src_all/`), it must not be committed.
- GitHub keeps unreferenced old commits reachable by hash for a while. Before making the repo
  public, ask GitHub Support to purge cached views of the old `windows-native` commits (or publish
  from a fresh repo).
- The old Linux root never had the shader. It brought `tools/scripttools/fn2name.txt` (script
  native names, community NativeDB style, not from the game files) and address tables
  (`ptrs.txt`, `handlers.txt`, `buckets.txt`, `pro1.natives`); keep those out of the release
  package.

## Linux: what to build

Goal: `gtav-recomp-linux-x64.tar.gz` (or an AppImage) with the same promise. Attach it to its own
tag (`v0.1.0-linux`) or to the Windows release. Same `expected.txt`, same manifest and partition
seed.

1. **Game shared library instead of a DLL.**
   - `gtav/fs/gtav_pch.h`: replace the `__declspec(dllimport)` bridge and `log2f` declarations
     with plain `extern` (ELF resolves them at load time); keep everything else. Wrap in
     `#ifdef _WIN32` so one header serves both.
   - `gtav/fs/game_dll.cpp`: `__attribute__((visibility("default")))` export of
     `gtav_game_image_config`.
   - Compile: `clang++ --target=x86_64-linux-gnu -nostdinc++ -nostdlibinc -fPIC -O2 -std=gnu++23
     -march=x86-64-v3 -msse4.1 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-math-errno
     -w -isystem fs/include -I gen` (check the shims in `fs/include` against what clang's own
     headers pull in on Linux).
   - Link: `ld.lld -shared --no-undefined` will fail on purpose; use `--allow-shlib-undefined`
     and leave the hooks, bridge functions and `log2f` undefined, to be resolved against the exe
     (link the exe with `-Wl,--export-dynamic` or a `--dynamic-list` of the hook + bridge
     symbols, which is what `gen_interface.py` already computes) and `libm`.
     Symbol visibility: the `.so` must export the `__imp__sub_*` functions the exe calls.
   - `gen_interface.py`: add a Linux mode that writes `gtav_hooks.h` (same) plus a dynamic-list /
     version script instead of `.def` and import libs. Read the object files with `llvm-nm`.
   - `main.cpp` under `GTAV_GAME_DLL`: `dlopen("./gtav_game.so")` + `dlsym`, or link the exe
     against the `.so` with `$ORIGIN` rpath.
2. **Launcher.** `loader/src/install.cpp` and `sources.cpp` are mostly portable C++. Replace
   CreateProcess (Child), BCrypt SHA-256 (a small embedded SHA-256 avoids an OpenSSL
   dependency), LoadLibrary checks, MoveFileEx, GlobalMemoryStatusEx; the Win32 window in
   `main.cpp` becomes a terminal UI (`gtav-setup DISC1 DISC2 TU`) and optionally zenity/kdialog
   file pickers. Keep `--unattended` semantics for the test.
3. **Toolchain in the package**: LLVM 19.1.5 Linux release tarball: `clang` (as `clang++`),
   `ld.lld`, `lib/clang/19/include`, `LICENSE.TXT`. Must not require system headers or glibc dev
   packages (the build is freestanding).
4. **Shaders**: `shader_compiler.cpp` already has the non-Windows paths (`XenosRecomp`, `dxc`).
   Ship `dxc` + `libdxcompiler.so` from `thirdparty/XenosRecomp/thirdparty/dxc-bin` and make
   `Run()` set `LD_LIBRARY_PATH` (or rpath the dxc binary). Do not ship `build/spirv`.
5. **Codegen determinism on Linux**: run the Linux `rexgluerd` on the same inputs and compare
   with the `gen` lines of `tools/release/expected.txt`. If anything differs (line endings,
   path text), make the Linux installer check against Linux-made gen lines, and say so here.
6. **Packaging**: runtime `.so` files (rexruntime, rexgpu-xenos, Tracy), the exe, the codegen
   tool, the launcher, `tools/`, `LICENSES/` (reuse `make_release.py`'s license collection),
   `README.txt` adapted (glibc floor 2.35 from the SDK CI container; Vulkan; AVX2; controller).
7. **Taint scan**: `taint_scan.py` is portable; write `scan_release.sh` with Linux controls
   (`/usr/lib/x86_64-linux-gnu/libc.so.6`, `libm.so.6`, `libstdc++.so.6`, `libvulkan.so.1`, the
   SDK `src/ include/ resources/templates`). Sources: `basefile_tu27.bin`, the TU xexp,
   `update.rpf`, the Linux `build/spirv` and shader cache, the generated tree. Release only on
   **PASS, 0 failing**.
8. **End-to-end**: install from the ISOs with `--unattended` into a copy of the package, boot it
   headless, check: hashes OK, gen match, shared library loads, in world after A presses,
   shaders compile with 0 failures.
9. **Corresponding source** (GPL/LGPL, below): tag the commit the Linux binaries are built from,
   run `tools/release/make_source_archive.sh` on the Linux SDK/XenosRecomp/plume trees, upload
   the archive next to the Linux package. If the Linux SDK tree is not exactly v0.10.0 +
   `rexglue-sdk-gtav-linux.patch` (check: `git diff HEAD` plus the two untracked
   `thirdparty/imgui_rex_*` files must equal the patch), commit the difference into the patch
   first.

## Licensing and source (decided 2026-10-29)

- This project: **GPL-3.0-or-later** (`LICENSE`). Required anyway: the SDK links binutils' PowerPC
  disassembler (GPL-3).
- **FFmpeg** (LGPL-2.1-or-later, configured with `CONFIG_GPL 0`: mp3/wmav2/xmaframes decoders) is
  linked **statically** into the SDK runtime. Compliance: the release carries
  `LICENSES/third_party/FFmpeg-NOTICE.txt`, and every release has a **complete corresponding
  source archive** (`gtav-recomp-src-<tag>.tar.xz`, `tools/release/make_source_archive.sh`):
  this repo at the tag, the SDK + every submodule (FFmpeg included) at the exact commits, XenosRecomp
  and plume + submodules, with our patches and `SOURCE.txt` saying how to rebuild/relink with a
  modified FFmpeg. Upload it with every binary release, from the same commits.
- The Xbox 360 XEX retail key in the runtime stays (owner decision: Xenia ships it too).
- dxc/dxcompiler are Microsoft open source (NCSA): shipped. `dxil.dll` is not shipped.
- The Visual C++ runtime is not bundled (the README links Microsoft's installer).
