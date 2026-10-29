# GTA V recomp: macOS port, then Android (start here on the Mac)

Written 2026-10-29 on Linux for a fresh session on the owner's Mac (Apple Silicon, Android Studio
installed). The game runs well on Linux and Windows-era code: native renderer, ~100-117 fps on a
laptop RTX 4050 after the 10-29 performance pass. Goal now: **macOS first** (arm64, shares all
ARM work with Android), **then Android** from the Mac. Owner's aim: it should run well on mobile.

**Rule:** never use the leaked Rockstar source ("GTAVCleanRoomReference"), not even for names.
Game files and anything derived from them (shaders, SPIR-V, disassembly) never go into git.

Read first, in this order: this file; `docs/HANDOFF_WINDOWS_MAC_2026-10-29.md` (what changed in the
perf pass and how portable each piece is, section 4 = macOS); `docs/HANDOFF_WINDOWS_MAC.md`
(repo layout, third-party pins, renderer design, hard-won facts); bottom of
`docs/NEXT_SESSION_2026-10-29.md` (perf pass details, Android notes); `docs/d3d_map.md`.

## 0. What the owner brings

- **Flash drive** with the two game ISOs (Disc 1 Install, Disc 2 Play) and the folder
  `gtav_mac_bundle_2026-10-29/` (made on Linux; game-derived, private):
  - `assets/default.xex` (disc 2) + `default.xexp` (TU27 disc-2 patch): codegen input.
  - `tu27/tu00000009_00000000` (the TU27 title update package) and its extracted `disc001/`,
    `disc002/` (`default.xexp`, `update.rpf`). **TU27 is required**: the base disc build hangs in
    the prologue.
  - `update/update.rpf`: the runtime `update:` root.
  - `spirv.tar.gz`: the current precompiled shaders (3,665 `.spv` + `vs_*.indirect` markers;
    includes the 10-28 water/shadow fixes). Unpacks to `spirv/`.
  - `native_shaders/*.h`: the renderer's own shaders compiled by dxc on Linux (blit, rect list,
    resolve, menu, tile classify): lets the game build before dxc works on the Mac.
- If any of these are missing, ask the owner before improvising (e.g. the TU27 package).

## 1. Set up the tree

```sh
git clone https://github.com/andrewnakas/gtav-recomp ~/gtav-recomp && cd ~/gtav-recomp
git checkout windows-native
git config user.name andrewnakas
git config user.email 7544678+andrewnakas@users.noreply.github.com   # maintainer's request
# third-party pins (see HANDOFF_WINDOWS_MAC.md):
git clone https://github.com/rexglue/rexglue-sdk rexglue-sdk && git -C rexglue-sdk checkout f5337cd
git -C rexglue-sdk submodule update --init --recursive
git -C rexglue-sdk apply ../rexglue-sdk-gtav-linux.patch      # ALL SDK changes (superset)
mkdir -p thirdparty
git clone --recursive https://github.com/renderbag/plume thirdparty/plume && git -C thirdparty/plume checkout 0ce184b
git -C thirdparty/plume apply ../../plume-gtav.patch
git clone --recursive https://github.com/hedge-dev/XenosRecomp thirdparty/XenosRecomp && git -C thirdparty/XenosRecomp checkout 990d03b
git -C thirdparty/XenosRecomp apply ../../xenosrecomp-gtav.patch
```
(If a patch does not apply cleanly, check the pin and `git submodule update` first; the patches
are made against exactly these commits.) Tools: Xcode command-line tools (clang), `brew install
cmake ninja python`; Python 3 for the scripts (`pip install cryptography` only if
`scripts/extract_basefile.py` is needed).

Untracked new files in the SDK (`git -C rexglue-sdk status`): after SDK edits, regenerate the
patch with `git -C rexglue-sdk add -N <new files>` then `git -C rexglue-sdk diff >
rexglue-sdk-gtav-linux.patch` (one patch for all platforms; keep platform code `#ifdef`'d).

## 2. Game data (from the ISOs) — on an APFS (case-insensitive, the default) volume

Xbox paths are case-insensitive; keep the data on a case-insensitive volume (macOS default; not
a case-sensitive APFS). Layout used on Linux (`<G>` = e.g. `~/gtav-data`):

```sh
python3 -I scripts/xiso.py extract "<Disc 2 (Play).iso>" <G>/game/disc2_files      # runtime game: root
python3 -I scripts/xiso.py extract "<Disc 1 (Install).iso>" <G>/game/disc1_files content/
for n in 0 1 2 3; do  # disc-1 install partitions -> the "installed" content
  python3 -I scripts/stfs_extract.py extract \
    <G>/game/disc1_files/content/0000000000000000/545408A7/00000002/545408A70000000$n \
    <G>/userdata/0000000000000000/545408A7/00000002/545408A70000000$n
done
mkdir -p <G>/game/update && cp <bundle>/update/update.rpf <G>/game/update/
cp <bundle>/tu27/disc002/default.xexp <G>/game/disc2_files/        # disc2_files has it on Linux too
cp <bundle>/assets/default.xex <bundle>/assets/default.xexp gtav/assets/
mkdir -p build/gtav && tar -C build/gtav -xzf <bundle>/spirv.tar.gz  # -> build/gtav/spirv
```
Runtime flags (see `play.sh`): `--game_data_root=<G>/game/disc2_files --user_data_root=<G>/userdata
--update_data_root=<G>/game/update --gpu_plugin=xenos --headless=true`.

## 3. Milestones (plan the first session around M1-M2; confirm the plan with the owner)

**M1 — SDK on macOS arm64.** `cmake --preset mac-arm64` (the SDK has `mac-arm64` presets,
`MapViewsMac` for the 4.8 GB guest arena, SIMDe for SSE on ARM, MoltenVK and dxc in
`thirdparty/`). Build Release, `cmake --install`. Expect fixes in: memory views (host = guest +
0x100000000 mapping; `/dev/shm` does not exist on macOS), threading (`threading_posix.cpp`),
SDL audio. `--audio_device_frames=256` (SDL hint) matters: the game's update thread waits for an
audio pass every frame; check with `--gtav_wait_profile=true` that `pass start` lines in the
`[waits] timeline` are ~5.3 ms apart (bursts of four = capped fps).

**M2 — codegen + game build, emulated renderer first.** `rexglue codegen gtav/gtav_manifest.toml`
(from `gtav/`; ~10 min; the manifest is CRLF, keep it so) writes `gtav/generated/`. Configure the
game with the SDK's install prefix (`CMAKE_PREFIX_PATH`), preset `mac-arm64`. The x86-only CMake
block (`-mcmodel=small -march=x86-64-v3`) is skipped on Apple. Boot with `--gtav_native=off`
(the SDK's emulated Vulkan renderer through MoltenVK) to prove the CPU side: title screen,
prologue, world. Write `play_mac.sh` (copy `play.sh`: drop the `/dev/shm` cleanup and
`--vulkan_device`).

**M3 — native renderer on the Mac.** Port, in this order:
1. x86 intrinsics in `gtav/src/native/backend.cpp` (`__attribute__((target("ssse3")))`,
   `_mm_shuffle_epi8` in `ConstantUpload` and `SwapIndices`, `immintrin.h`): use SIMDe like the
   SDK (`simde/x86/ssse3.h`), which gives NEON on arm64. Android needs the same.
2. `WaitMultiple` in the SDK still polls in 1 ms steps on macOS (the futex code is
   `#if REX_PLATFORM_LINUX`): port with `os_sync_wait_on_address` (macOS 14.4+) /
   `__ulock_wait`, keeping the per-handle `multi_waiters_here_` registration.
3. Renderer backend: quickest is the existing Vulkan path on MoltenVK. Needs buffer device
   addresses (MoltenVK supports them on Metal 3 GPUs) and **no geometry shaders** (Metal has
   none): rect lists (`native_rectlist.hlsl`) must be expanded on the CPU or in the vertex shader.
   The proper path later is Plume's Metal backend + shaders via SPIRV-Cross or Apple's Metal
   Shader Converter (UnleashedRecomp does the latter).
4. dxc on the Mac (for the CMake-built renderer shaders and the runtime shader compiler): the
   SDK has `thirdparty/dxc`; until it works, drop the bundle's `native_shaders/*.h` into
   `build/gtav/native_shaders/` and skip that CMake step.
5. Write watch (`write_watch.cpp`) is Linux-only (userfaultfd): it switches itself off and the
   renderer hashes textures/buffers instead — correct, a few ms slower.

**M4 — perf on the Mac** with the same tools: `[native]` log lines every 300 frames,
`--gtav_wait_profile=true`, A/B a runtime setting within one run through `logs/menu_set`
(`tools/native/ab.sh` shows how; scenes differ between runs).

## 4. Android (after the Mac works)

Build from the Mac with Android Studio's NDK (arm64-v8a), reusing the arm64 work above.
- App shell: SDL3's Android support (the SDK uses SDL3) or a NativeActivity; Vulkan on the
  device's GPU (the Vulkan renderer runs as is; no geometry shaders on many mobile GPUs either —
  the M3 rect-list change covers it).
- Memory: the guest arena is a 4.8 GB shared mapping plus views; on a phone use an anonymous
  or `ashmem`/`memfd` backing with overcommit; check the device's address space and RAM.
- Write watch: Android apps are normally denied userfaultfd (SELinux) and PAGEMAP_SCAN needs
  kernel 6.7+, so expect the hashing fallback; phone CPUs make hashing costlier — revisit then.
- Audio: AAudio via SDL; keep the 256-sample (5.3 ms) period check.
- Game data (~8 GB extracted) on the device: app-specific external storage; Xbox paths are
  case-insensitive but Android's storage is case-sensitive (the SDK may need a case-folding
  lookup, or lowercase the extracted tree consistently).
- Input: the controller path (SDL gamepad) first; touch later.

## 5. Owner preferences (from the Linux sessions)

- Plan before big work, then execute; keep test runs short and say what each run decides;
  prefer fixes over long chains of diagnostic runs.
- The owner play-tests and reports; warn before killing a game they are playing.
- Commit with the identity above; regenerate `rexglue-sdk-gtav-linux.patch` after SDK changes;
  push when the owner confirms; end commit messages with the Claude attribution lines.
- Write a handoff / next-session note in `docs/` at the end of each session.
