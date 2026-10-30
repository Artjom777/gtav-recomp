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
  -DCMAKE_C_FLAGS=-mcpu=apple-m1 -DCMAKE_CXX_FLAGS=-mcpu=apple-m1 (was -march=armv8-a: ~3% slower)
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

## Session 2 (2026-10-29 evening): native rendering fixed, first perf pass (uncommitted)

Rendering (prologue checked against the emulator and by eye across ~10 rooms + a cutscene):
- **Rect lists without geometry shaders** (`rect_gs` false on MoltenVK): `RectGather` / `RectExpand`
  in `backend.cpp` expand each rect on the CPU from the host-converted vertex inputs (corner opposite
  the longest POSITION edge, fourth = a + b - corner, drawn as 6 vertices). The tiled-lighting
  rects' indirect-fetch VS (290D4F388D24593F, 1702372CA21BF56A) take their fetch index from
  POSITION.zw (same on every corner), so they go through the same path. Fixed the half-screen and
  triangle-checkerboard lighting.
- **Triangle fans -> lists** on the CPU (with quad lists): Metal has no fans.
- **MoltenVK fast math off** (`MVK_CONFIG_FAST_MATH_ENABLED=0`, set in `renderer.cpp` before any
  Vulkan instance): fast math broke `clamp(rsqrt(0)) * 0` -> NaN; the radar showed only outlines.
  No measurable fps cost.

Perf (heavy opening room, 1021 draws): **46 -> 57 fps**, lighter rooms 55-63 -> 62-68.
- Metal System Trace: GPU-bound, but the GPU is busy only ~11 ms of a ~25 ms frame; the rest is
  gaps between ~300 encoders (render passes) per frame (~18-45 us each, tile load/store).
- Fix: depth/stencil-only draws and clears into the bound pass's depth target keep its colour
  attachments with writes off (`colorless` in DrawImpl / Clear) instead of a depth-only pass
  (light-volume stencil marks alternated with lighting draws): 260 -> 206 render passes.
- Measured and rejected: async queue submits (no change), resolves as blit copies (slower, 53),
  deferring writeback dispatches to frame end (no change).
- Upper bound: skipping colour resolves entirely gave ~78 fps (139 passes): resolves (72 per frame,
  31 forcing the source pass to restart) are the remaining ~9 ms. Next idea: resolve inside the
  source pass (framebuffer fetch / input attachment + image store) so neither the copy pass nor the
  restart is needed; also DONT_CARE load for full-coverage resolve destinations (~1 ms).
- Stats line now includes `per frame N render passes, M barriers` (plume counters, patch updated).
- Then CPU: record ~5.6 ms + MoltenVK encode ~4.9 ms per frame (~90 fps ceiling).

Machine: `xctrace` Metal System Trace buffers GBs in /private/var/folders/.../T/instruments*.ktrace
and filled the disk once (delete those files after a trace; record <= 3 s).

## Session 3 (2026-10-29 night, owner AFK): skybox flashing fix, perf investigation (uncommitted)

Skybox / mesh flashing (owner report): the owner's play log had 746 frames with "lost draws: N
pipelines compiling" (bursts up to 1,574): draws were skipped for a frame while MoltenVK compiled a
new pipeline (SPIR-V -> MSL -> Metal, tens of ms each) beyond the 40 ms per-frame wait. (Not missing
shaders: the "ps without SPIR-V" count is depth-only draws, ps = 0.) Fixes in backend.cpp:
- `PipelineWarmer`: every pipeline created is appended to `build/gtav/shader_cache/pipelines.bin`
  (key, shader hashes, every description field via `PipelineFields`); at boot 4 workers compile
  them all again. A warmed pipeline is used only if its fields equal the description built now
  (code changes can't pick up stale state; a mismatch re-records it). Log: "pipeline warm-up".
  Second run of the prologue: 0 lost draws (first run: a 186-draw burst).
- `--gtav_native_hold_lost_frames` (default on): the last complete front buffer is copied each
  frame (no measurable cost); a frame that still lost draws (first-ever pipelines) shows that copy
  instead (log "showing the previous frame instead"), up to 60 frames in a row.
- `MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION=1` (renderer.cpp): faster first-time bursts.

Perf (heavy opening room, 1,021 draws, ~57 fps before). Metal System Trace: the GPU is busy ~43%;
a frame is a serial chain of ~475 vertex/fragment intervals with ~31 us (fragment -> next vertex)
and ~19 us (vertex -> fragment) gaps, most passes doing < 10 us of work. GPU clocks at maximum.
- Kept: the frame's rendering is submitted in its own command buffer, only the window copy waits for
  the drawable (frames now start back to back on the GPU; was up to 7 ms idle between frames), and
  a submit thread runs vkQueueSubmit/present (MoltenVK encodes the Metal command buffer inside
  vkQueueSubmit: 3.5 ms per 1,000 draws now off the render thread; matters when CPU-bound: the
  owner's own play log was CPU-bound, record ~11 ms + submit ~6 ms, while the Mac swapped 9 GB).
- Measured and rejected: in-pass resolves (MoltenVK still splits encoders: 224 -> 210 only),
  compute resolves (`--gtav_native_compute_resolves`, off: 206 -> 146 passes but slower, because
  STORAGE usage on resolve textures alone costs ~1 ms: 57.2 -> 53.8), `MVK_CONFIG_USE_MTLHEAP=2`
  (56.0), skipping vkCmdPipelineBarrier (59.0, not kept: relies on Metal tracking), no argument
  buffers (crash).
- Pass reasons per frame (`GTAV_NATIVE_PASS_STATS=1`, logs "PASSDIAG" + every 3000th frame's
  command sequence): 72 resolve copies, 61 target changes, 25 restarts after a resolve, 21 new
  targets after a resolve, 22 after writeback computes.
- Benchmarks: `GTAV_IGNORE_SETTINGS=1` (new) skips gtav_settings.ini, which has the owner's menu
  choices incl. `gtav_fps_limit=30`. Another Claude session's CPU-heavy job (load avg 7-9) made the
  last runs noisy (47-53 fps vs 57-59 on a quiet machine).
- Next ideas: macOS write tracking (no userfaultfd: buffers/textures are hashed every frame),
  merging consecutive resolves into one MRT pass, rendering post-chain draws straight into the
  resolve destination (needs care: EDRAM aliasing, coverage).

## Session 3b (2026-10-30 night): clear-sky slabs narrowed down, not fixed yet

Symptom (owner): over clear sky (not with clouds) big flat light-grey triangles / mountain shapes
flicker and change shape every frame. Same as the Linux "sky slabs" only in look: the Linux fixes
(fence copies, 6 ms lag, pipeline waits) are active on the Mac and are not the cause.

Established with live switches (logs/menu_set) while the owner stood outside (prologue, police
shootout behind the bank):
- The shapes are holes in the "distant hills / sky band" draw: vs CFA891AC2B21834E,
  ps DCFC61F75589ED79 (2 draws, 3360 + 1800 indices, static VB/IB, camera-centred mesh x2000,
  oPos = (x, y, w, w) = far plane). `gtav_native_skip_ps=DCFC61F75589ED79` removes the slabs.
  Before the draw the sky is light; the draw darkens the sky except in the holes.
- Not the cause (switched live, no change): fog strip (ps 9DFE7F549A326A57), the Mac colourless
  depth-only shortcut (`gtav_native_colorless=false`), depth clamp (`gtav_native_depth_clamp`),
  sky pin depth (`gtav_native_farpin_scale` 4 / 1000), `gtav_native_exact_cache`,
  `gtav_native_snapshot_draws` (breaks the minimap), held frames.
- The sky's depth is 0 (cleared) in the depth resolve, so the holes are not depth: the draw tests
  stencil EQUAL 7 (mask 7), and the sky's stencil must be != 7 there.
- Candidate writers of that stencil: deferred light volumes drawn before the hills, depth/stencil
  only (ps 0), triangle strips of 192 verts (16_16_16_16_FLOAT, stride 8): vs 2C40A3B0495893A9
  (32 per frame) and FA494CC72357CB0C, dc 28928F13 (z LESS, stencil ALWAYS, zpass REPLACE 0x40,
  zfail ZERO, writemask FF), then a mode-5 pass dc 28928963 sref FF6020. Police sirens near the
  player rotate every frame, which would explain shapes changing every frame.
- Next test (build ready): stand outside, then `echo gtav_native_skip_vs=2C40A3B0495893A9,FA494CC72357CB0C
  > build/gtav/logs/menu_set`; if the holes vanish, compare these light-volume draws (vertex
  decode of 16_16_16_16_FLOAT, strip winding/cull with face bit, stencil ops/back face) with Linux.
- Tools: M = screenshot (F10), N = frame dump (F9), logs/dump_request, logs/menu_set name=value,
  GTAV_NATIVE_SHOT_DIR=<dir> GTAV_SHOT_SECS=2 periodic shots; build/gtav/logs is a symlink to the WD
  drive (/Volumes/Untitled/gtav-captures/live). SDK input_system now reloads --input_script when
  the file changes (live driving), not in the SDK patch yet.
- Two crashes (guest NULL write on thread F800002C, log spam 700 MB/min) right after the bank intro
  in runs exp1/exp2; not reproduced in 4 later runs. Watch for "Unhandled guest access violation".
