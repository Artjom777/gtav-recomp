# macOS bring-up handoff (2026-10-29, Apple M1 MacBook Air, 8 GB) — start here next session

Read with `docs/MAC_THEN_ANDROID_PLAN.md` (the plan this session followed). Goal of the next
session (owner): **native-renderer rendering on the Mac as correct as the Linux version**, then perf.

## Status

- M1 (SDK on macOS arm64) and M2 (codegen + game build) done. The game boots, plays the legal /
  calibration screens, loads Story Mode and runs the prologue on **both** renderers:
  - emulated (`GTAV_NATIVE=off`, SDK Vulkan renderer on MoltenVK): renders correctly, ~6 fps
    in-game when the Mac was swapping hard (other apps open); not re-measured since.
  - native (`GTAV_NATIVE=on`, Plume Vulkan on MoltenVK): ~1020 draws/frame, ~25 ms frames (~40
    fps) in the prologue, but **rendering is wrong** (owner: "very off"). Known cause #1 below.
- Data on the internal SSD (`~/gtav-ssd/game`, `~/gtav-ssd/userdata`, 15.4 GB): prologue reached
  ~1 min after launch (17 min from the USB GoFlex HDD). `play_mac.sh` picks `~/gtav-ssd`
  automatically. Backup copy: WD drive `/Volumes/Untitled/gtav-data` (copy was still running at
  session end; re-run `rsync -a ~/gtav-ssd/game ~/gtav-ssd/userdata /Volumes/Untitled/gtav-data/`
  to finish it). GoFlex `/Volumes/FreeAgent GoFlex Drive/gtav-data` also has everything
  (+ the bundle), but reads at 2-10 MB/s and hung once.

## Layout on this Mac

- Repo: `~/Documents/gtav-Recomp/repo` (branch `windows-native`, local commits not pushed).
  `rexglue-sdk/` (f5337cd + patch, ignored by git), `thirdparty/plume`, `thirdparty/XenosRecomp`.
- SDK build: `cd rexglue-sdk && cmake --preset mac-arm64 && cmake --build out/build/mac-arm64
  --config Release -j 6 && cmake --install out/build/mac-arm64 --config Release`, then copy
  `out/install/mac-arm64/lib/{librexruntime,librexgpu-xenos}.dylib` into `build/gtav/`
  (the exe's rpath is @executable_path; libMoltenVK/libvulkan were copied there once too).
- Codegen: `cd gtav && ../rexglue-sdk/out/install/mac-arm64/bin/rexglue --force codegen
  gtav_manifest.toml` (4.5 min).
- Game build dir: `build/gtav` (Ninja, Release). Configured with
  `cmake -S gtav -B build/gtav -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_C_FLAGS=-march=armv8-a -DCMAKE_CXX_FLAGS=-march=armv8-a
  -DCMAKE_PREFIX_PATH=$PWD/rexglue-sdk/out/install/mac-arm64
  -DGTAV_PREBUILT_NATIVE_SHADERS=<bundle>/native_shaders`; build `ninja -j 4` (8 GB RAM).
  Precompiled game SPIR-V in `build/gtav/spirv` (from the bundle).
- Bundle `gtav_mac_bundle_2026-10-29` is on the GoFlex and on the phone (`/sdcard/Download/`).
- Run: `GTAV_NATIVE=on ./play_mac.sh <log>`; logs in `logs/<log>.log`.
  Keyboard plays as a pad (`--mnk_mode`): Space=A, Backspace=B, Return=Start, WASD, arrows.
- Unattended checks: macOS `screencapture` of the game window returns **black** (even when the
  game draws), so use the game's own captures: emulated `GTAV_SHOT_DIR=<dir> GTAV_SHOT_SECS=n`,
  native `GTAV_NATIVE_SHOT_DIR=<dir> GTAV_SHOT_SECS=n` (PPM; `sips -s format png`). Delete old
  shots (2.6 MB each; disk is tight). `--input_script=build/press_a.txt` (A every 5 s) walks
  calibration -> Story Mode -> prologue.
- `scripts/xiso_adb.py`: extracts from an ISO on the phone over `adb exec-out dd` (no ISO copy).

## Changes this session (commits e8f4696, 70a41d5 + this one)

- `byteswap_simd.h` (SSSE3 / NEON swaps), `safe_read.h` (mach_vm_read_overwrite), macOS paths in
  `paths.h` / `wait_profile.cpp`, `renderer.cpp` RenderWindow = NSWindow + CAMetalLayer from SDL.
- CMake: `GTAV_PREBUILT_NATIVE_SHADERS` (no dxc on the Mac), Linux link flags/tools skipped on
  Apple, `gtav_register.cpp` at `-O0 -fno-global-isel`, no PCH (arm64 backend took 40+ min).
- SDK patch: VulkanPresenter never stored `swapchain_format` -> rebuilt the final pipeline every
  frame (SPIR-V->MSL each paint on MoltenVK); HostPathDevice skips the case-insensitive directory
  scan on case-insensitive macOS volumes (the game polls a missing file; each poll listed a
  directory: multi-second stalls).
- `play_mac.sh`.

## Next session: native rendering correctness

1. **Rect lists (primitive 8) — known, visible everywhere.** Linux expands them with
   `native_rectlist_gs` (geometry shader); Metal/MoltenVK has no geometry shaders, so only the
   first triangle of each rect is drawn: a fine triangle checkerboard over lighting/post passes
   (tiled lighting draws rects per tile). Fix in `DrawImpl` (`backend.cpp`, vertex streams ~line
   2408, draw ~2756): when `d.primitive == 8` and the device has no GS, build per-draw vertex
   data with 6 vertices per rect (corner, a, b, a, b, fourth) from the **host-converted** stream
   data (after `convert`: floats, int16 norms, floats-from-ints), fourth = a + b - corner per
   component (clamp for normalized types), corner = vertex opposite the longest edge measured on
   the POSITION element (usage 0); skip rects with w <= 0 / NaN like the GS does. Exact for
   affine vertex shaders (screen-space passes). Indexed rects: de-index first. Vertex shaders in
   `indirect_fetch_shaders` fetch raw guest streams by index: they need a different route
   (check whether any rect-list draws use them). Keep the GS path when the device supports GS
   (Linux/Windows). Pipeline: topology TRIANGLE_LIST without `desc.geometryShader`.
   Also check how Plume/MoltenVK reacts to the GS stage today (pipeline creation may fail or
   ignore it) — log `[native] frame ... lost draws` / pipeline errors.
2. Then compare against Linux captures for the other differences the owner sees (MoltenVK
   specifics: primitive restart can't be disabled — the strip/fan draws; buffer device address;
   formats; `fragment_shader_interlock`; depth clamp/bias; the `tile classify` replacement shader).
   Use `GTAV_NATIVE_SHOT_DIR` shots and the F9 frame dump (`logs/native_dump`).
3. Perf after correctness: ~40 fps at 1020 draws on the M1; `[native]` lines every 300 frames;
   `--gtav_wait_profile=true`; WaitMultiple still polls 1 ms on macOS (plan M3.2).

## Machine notes

- 8 GB RAM: close other apps (heavy swapping made the emulated run ~6 fps). Internal disk ~5 GB
  free after the SSD copy; build tree + logs live there.
- Other Claude agents ran on this Mac at the same time (offloading to the GoFlex); a concurrent
  rsync onto the nearly full GoFlex hung the drive until it was replugged.
