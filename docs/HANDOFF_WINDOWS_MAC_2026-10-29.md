# GTA V recomp: handoff for Windows and macOS (performance pass, 2026-10-29)

Written on Linux after the 10-29 performance pass (owner play-tested it: "that looks great").
Linux went from ~37-48 fps to ~96-107 fps moving around town and ~117 standing (hidden
benchmarks, RTX 4050 laptop, native renderer "on", uncapped). In the owner's own play session
(visible window, driving, VSync on) the render thread recorded 11.6-14.5 ms a frame on average
and the audio had no silent frames. Details and the list of what gated
each step: bottom of `docs/NEXT_SESSION_2026-10-29.md`. The older handoff
(`docs/HANDOFF_WINDOWS_MAC.md`, 10-25) still describes the repo layout, third-party pins and
build steps; this file only covers what changed and what each platform has to do.

**Rule that still applies:** never use the leaked Rockstar source, not even for names.

## 1. Get the code

- Branch `windows-native` on `https://github.com/andrewnakas/gtav-recomp`, commits
  `ca3622e`..`HEAD` (pushed with this handoff).
- **SDK patch: apply `rexglue-sdk-gtav-linux.patch` on every platform.** It is against the same
  SDK commit (`f5337cd`) as `rexglue-sdk-gtav-windows.patch` and is a superset of it (that file
  is from 10-28 and lacks the fence/idle/wake hooks and everything below). Platform-specific
  code inside it is `#ifdef`'d. Afterwards the old Windows patch can be deleted.
- Rebuild the SDK, install it, copy its shared libraries next to the game (as before), then
  rebuild the game. The game rebuild recompiles all 382 generated files (new compile flags).

## 2. What changed, and how portable each piece is

| Change | Where | Windows | macOS (arm64) |
|---|---|---|---|
| Diagnostic per-draw hash off by default (`--gtav_native_check_draw_data`) | gtav capture/backend | works | works |
| Capture copies only the constant registers shaders read; renderer swaps only those | gtav + SDK `rex_shader_float_constant_bitmap` (exported from librexgpu) | works (dllexport) | needs the SSE fallback below |
| Vertex layout memo, buffer memo, exponent scale without `ldexp`, SSSE3 index swap | `gtav/src/native/backend.cpp` | works | **x86 intrinsics: needs a fallback** |
| Write watch: the kernel reports guest writes, no per-frame texture/buffer hashing | `gtav/src/native/write_watch.cpp` | **Linux only**: off, falls back to the old hashing (correct, ~3-5 ms/frame slower) | same: off, hashing |
| Command processor waits per draw (not per frame) at fences of the frame being recorded | `capture.cpp` (`DrawsRecorded`) | works | works |
| Fence-wait expedite: kicks let through while the game blocks on a GPU fence; CP woken | `gtav/src/gpu_fence_wait.cpp`, SDK `rex_skip_rendering_gpu_waiters` / `rex_skip_rendering_wake` | works (GetProcAddress / dllexport paths written, untested) | works |
| `WaitMultiple` futex wake instead of 1 ms polling, per-handle | SDK `threading_posix.cpp` | n/a (Windows uses native `WaitForMultipleObjects`) | **still polls 1 ms**: port it (see below) |
| Type-0 packets as range writes; known-register table; no shader loads under `gpu_skip_rendering` | SDK `command_processor.cpp` (shared base class) | works (D3D12 CP inherits it) | works |
| Audio device period 256 samples (`--audio_device_frames`, SDL hint) | SDK `sdl_audio_driver.cpp` | check (see below) | check |
| `-mcmodel=small -march=x86-64-v3` for the game code | `gtav/CMakeLists.txt` (`UNIX AND NOT APPLE`, x86-64 only) | not applied; consider the clang-cl/MSVC equivalent (`/arch:AVX2`) | n/a |
| Esc menu "Low GPU latency" (CP 2 ms behind instead of 6; +23% fps in an A/B run) | `gtav/src/native/menu.cpp` | works | works |
| PGO option (`GTAV_PGO`, off; ~3% only) | `gtav/CMakeLists.txt`, `src/pgo_dump.cpp` | clang only | clang only |
| Tools: `tools/native/bench.sh`, `ab.sh`, `--gtav_wait_profile` | shell / gtav | bench scripts are sh; `--gtav_wait_profile` works everywhere | same |

## 3. Windows to-do

1. Apply `rexglue-sdk-gtav-linux.patch` instead of the Windows one. Build. Boot once with
   `play.ps1 -Counter`; check the log for `[native] write watch` (should say nothing / off) and no
   `cannot register` errors.
2. **Audio period (the biggest single win on Linux).** The game's update thread waits for an
   audio pass every frame; if the audio device takes 1024-sample periods, the guest's audio
   engine runs four 5.3 ms passes in a burst every 21 ms and the game is capped near 94 fps (47
   in heavier frames). Run with `--gtav_wait_profile=true` for 60 s and read the
   `[waits] timeline` block in the log: the `pass start` lines must be ~5.3 ms apart. If they
   come in bursts, WASAPI ignored the SDL hint; then make the SDK audio driver release one guest
   frame per 5.3 ms itself (a pacing timer in `sdl_audio_driver.cpp` instead of releasing the
   semaphore from the callback).
3. **Write watch on Windows** (optional, ~3-5 ms/frame of render thread): userfaultfd does not
   exist. `GetWriteWatch` needs `MEM_WRITE_WATCH` at allocation and does not work on the
   SDK's file-mapped views. Don't use the SDK's own mprotect watch for it: on Linux it stalled
   the game up to 250 ms (global lock + mprotect per arm). Options: keep hashing (default), or
   re-map the guest views with `VirtualAlloc2` + placeholders (bigger SDK change).
4. Compiler flags: Windows builds don't get `-march=x86-64-v3`; with clang-cl add `/arch:AVX2`
   (MOVBE needs `-mmovbe` too) to the `gtav_recomp` target; check whether the Windows build
   also uses an indirect-call code model for the generated code (Linux had `-mcmodel=large`
   from the SDK helpers: every call indirect).
5. `play.ps1`: nothing required (the menu sets the GPU lag); optionally mirror `play.sh`'s
   `GTAV_CP_LAG`.
6. Play-test: driving flicker, phone, shadows/car reflections, water, audio.

## 4. macOS to-do (arm64)

1. **x86 intrinsics in `backend.cpp`** (some predate this pass: `ConstantUpload`; new:
   `SwapIndices`, the run-based `ConstantUpload`): `__attribute__((target("ssse3")))` +
   `_mm_shuffle_epi8`. Add `#if defined(__x86_64__)` guards with a NEON (`vrev32q_u8` /
   `vrev16q_u8`) or scalar `__builtin_bswap32/16` path. The Android port (next Linux session,
   also arm64) needs exactly the same, so do it once in a shared helper.
2. **`WaitMultiple` still polls in 1 ms steps on macOS** (`threading_posix.cpp`, the futex code
   is `#if REX_PLATFORM_LINUX`). Port the same scheme: a global epoch word + per-handle
   `multi_waiters_here_`, waiting with `os_sync_wait_on_address` (macOS 14.4+) or `__ulock_wait`,
   falling back to the 1 ms sleep. On Linux the polling cost ~1 ms per guest multi-wait.
3. Write watch: off (no userfaultfd); hashing works. A later option is Mach VM write tracking,
   not worth it until the rest runs.
4. Audio period: same check as Windows step 2 (CoreAudio usually honours small periods).
5. Code model: the CMake block is Linux/x86 only; nothing to do on arm64.

## 5. Measuring on another platform

- fps and per-stage timings are in the log every 300 frames: `[native] fps=`, `record ms per
  frame`, `presented ... record avg`, `write watch per frame`, `capture constants per frame`.
- `--gtav_wait_profile=true`: per thread, where it waits (`[waits] <thread> waits N% | ...`),
  who releases the semaphores, a 400-event audio/update timeline, GPU fence wait stats.
- Compare settings within ONE run (scenes differ between runs): flip a setting through the
  `logs/menu_set` test hook (`name=value`), as `tools/native/ab.sh` does.
