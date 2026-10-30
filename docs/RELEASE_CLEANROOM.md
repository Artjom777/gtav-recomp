# Clean-room releases (Windows and Linux)

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

## How the Linux release works (built 2026-10-30)

Same promise and same reference data as Windows: `tools/release/expected.txt`, `gtav/gtav_manifest.toml`
and the partition seed, now committed as `tools/game_dll/codegen.partition.json` (copied from the
Windows release; addresses only). Package: `gtav-recomp-linux-x64.tar.gz`.

Player side (`loader/`, `gtav_launcher`, a terminal program; `src/main_linux.cpp`):
- `./gtav_launcher` starts the game when installed, otherwise runs setup; `--setup [DISC1 DISC2 TU]`
  (paths asked for when missing, remembered in `data/launcher.ini`), `--unattended` (same contract
  as Windows: `data/launcher.ini`, `data/logs/unattended.txt`, every question answered no),
  `-- ARGS` passed to the game (e.g. `--vulkan_device=1`). Ctrl+C cancels; setup resumes.
- `install.cpp` / `sources.cpp` are shared with Windows; the POSIX parts are `#ifdef`ed: pread file
  reads, an embedded SHA-256, posix_spawn children (nice 10), statvfs, rename, UTF-8 Widen/Narrow.
  Preflight: AVX2/BMI2/FMA, `libvulkan.so.1` loadable, package files present. Child commands are
  argument vectors on both platforms (Windows joins them into the same quoted command line).
- Free-space check counts every expected file already in place (disc 2, partitions, TU), so a
  resumed install no longer asks for room for the partitions again (both platforms).
- Codegen: `rexglue` (RUNPATH `$ORIGIN` finds `librexruntime.so` at the package root). **Linux
  codegen is byte-identical to Windows**: all 811 `gen` lines of `expected.txt` matched a Linux
  run (697 s single-threaded on the laptop), so one `expected.txt` serves both.
- Compile with the bundled clang 19.1.5 (`tools/llvm/bin/clang++`), flags in `install.cpp` and
  `tools/game_dll/build_game_dll.py`: `--target=x86_64-linux-gnu -nostdinc++ -nostdlibinc -fPIC
  -fno-semantic-interposition -fno-stack-protector -fcf-protection=none -O2 -std=gnu++23
  -march=x86-64-v3 -msse4.1 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-math-errno`.
  The `fs/include` shims are enough on Linux too.
- Link `gtav_game.so` with the bundled `ld.lld`: `-shared -soname gtav_game.so -Bsymbolic -z now
  --version-script=gtav_game.exports`. The version script exports only what `gtav_fast` imports
  (2931 `__imp__sub_*` + `gtav_game_image_config`), everything else is local, and `-Bsymbolic`
  keeps calls between game functions direct (no PLT). Hooks, the `gtavfs_*` bridge, kernel
  imports and `log2f` stay undefined and bind at load time to `gtav_fast` (its `--dynamic-list`),
  `librexruntime.so` and `libm`; `-z now` makes a missing one fail at start, not mid-game.
- Play: the launcher `exec`s `gtav_fast` (same arguments as Windows minus `--input_backend=xinput`).
  `gtav_fast` needs `gtav_game.so` (DT_NEEDED, RUNPATH `$ORIGIN`).

Developer side:
- `gtav/CMakeLists.txt` `GTAV_GAME_DLL=ON` on Linux: output `gtav_fast`; the pre-link step
  (`gen_interface.py`, Linux mode, reads `.o` files with llvm-nm) writes `gtav_hooks.h`,
  `gtav_fast.dynlist`, `gtav_game.exports` and a stub `gtav_game.so` (one `ud2` per export, soname
  `gtav_game.so`) that the exe links against in place of an import library. Hook/import sets are
  identical to the Windows `.def` files (2930 hooks, 2931 imports, 9 bridge).
- `fs/gtav_pch.h`: `GTAVFS_IMPORT` (dllimport on Windows, nothing on Linux); `game_dll.cpp` and
  `src/game_bridge.cpp`: `GTAVFS_EXPORT` (dllexport / default visibility).
- **`REX_PHYS_HOST_OFFSET` is platform-dependent** (SDK `pch_h.inja`: 0x1000 for the E0 view only
  on Windows and Apple ARM64, 0 elsewhere). The header first had the Windows value everywhere: on
  Linux every game access above 0xE0000000 (D3D command buffers) went 4 KB off, the command
  processor executed garbage, never reached a fence and the game hung ~20 s into boot spinning in
  the fence wait (8380CEF8/83807DC8) with "long wait" warnings. Any other platform branch in
  `pch_h.inja` must be carried over the same way (the Mac port: the `__APPLE__` DEFINE_REX_FUNC).
- Bundled LLVM: the official `LLVM-19.1.5-Linux-X64.tar.xz` (`clang-19` -> `clang++`, `lld` ->
  `ld.lld`, `lib/clang/19/include`). Its `lld` needs `libxml2.so.2` (only for lld-link manifest
  merging) and many distributions no longer have that soname, so `tools/llvm/lib/libxml2.so.2` is
  our own stub (`tools/release/lld_libxml2_stub.c` + `.map`: the 16 imported names, each traps);
  lld's RUNPATH `$ORIGIN/../lib` finds it. The tarball has no top-level license: `LLVM.txt` comes
  from the Windows release's LICENSES (same 19.1.5 license).
- Shaders: `tools/XenosRecomp` (RUNPATH rewritten in place to `$ORIGIN/lib` by the packager),
  `tools/dxc` (wrapper) + `tools/dxc-linux` + `tools/lib/libdxcompiler.so`; `libdxil.so` stays out
  (as `dxil.dll` does).
- `tools/release/make_release_linux.py OUT --llvm DIR --expected tools/release/expected.txt
  --licenses DIR [--compat] [--tar]` (reuses `make_release.py`'s license collection; strips debug
  info from our binaries), `tools/release/build_release_linux.sh` (native build + package),
  `tools/release/scan_release.sh` (Linux controls: libc, libm, libstdc++, libvulkan, libz, libzstd,
  libX11, libcrypto, SDK sources/templates, XenosRecomp sources). The Linux package ships
  `tools/shader_common.h` and XenosRecomp embeds it; XenosRecomp also writes it at the top of every
  HLSL file, so the dumped game HLSL in `derive/native/census2` matches it unless XenosRecomp's
  sources are a control. `layout_check.h` and `gtav_hooks.h` are skipped as sources:
  `build_game_dll.py` copies them from the release into the generated folder. The scan used to
  re-grow a long common run from every window inside it (quadratic, 6+ h on this package); it now
  skips windows already inside a measured run (~16 min).
- **Portable build**: binaries built on the laptop (Ubuntu 26.04) need glibc 2.43 / GLIBCXX_3.4.32.
  `tools/release/compat/build.sh` builds everything shipped in an Ubuntu 22.04 container
  (`compat/Dockerfile`: clang 20 + GCC 13 libstdc++, the SDK CI's toolchain) into `*-compat`
  trees with `-ffile-prefix-map` (no build paths); package it with `--compat`. Floor: glibc 2.35
  and GLIBCXX_3.4.32 (Ubuntu 24.04, Debian 13, Fedora 39, Arch; Ubuntu 22.04 needs the toolchain
  PPA's libstdc++). Container findings: 22.04's kernel headers lack userfaultfd async-WP and
  PAGEMAP_SCAN (write_watch.cpp now defines the uapi values; on a kernel older than 6.7 write
  watch switches itself off at run time); XenosRecomp must be configured with
  `BUILD_SHARED_LIBS=OFF` or it links libfmt.so (not shipped: every shader translation would
  fail). `make_release_linux.py` now refuses any shipped ELF that needs a library outside the
  system allowlist + the package. Smoke test on clean `ubuntu:24.04` and `debian:12` containers
  (every binary starts, the bundled clang/lld compile and link freestanding with no system
  headers): Ubuntu 24.04 passes; Debian 12 fails on GLIBCXX_3.4.32 as expected.
- Corresponding source: `make_source_archive.sh` (its `SOURCE.txt` now covers Linux too).

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
