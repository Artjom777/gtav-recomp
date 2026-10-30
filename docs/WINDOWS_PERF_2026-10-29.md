# Windows build + perf pass, 2026-10-29

Machine: i5-13420H (4P+4E), 32 GB, RTX 4050 Laptop. Game data on the E: USB HDD.
Build: `rexglue-sdk-gtav-linux.patch` on SDK f5337cd (`scripts\sdk_build_only.cmd`, copy
`out\install\win-amd64\bin\*.dll` into `build\`), then `build.cmd 4`. Play with `play.cmd`.

## Changes
- **CMake:** `-march=x86-64-v3` on Windows clang (option `GTAV_X86_64_V3`); link the
  `rexgpu-xenos(rd)` import lib (fixes the lld undefined `rex_skip_rendering_wake` symbols);
  `GTAV_SDK_COUNTERS` (OFF) strips the per-indirect-call perf counter atomics (~3% per game thread).
- **wait_profile.cpp:** Windows port (`sys/syscall.h` was the first build error).
- **frame_limit.cpp:** `gtav_no_power_throttling` (default on) opts the process out of
  EcoQoS / timer-resolution throttling. +~3% fps in an in-run A/B.
- **backend.cpp:** `gtav_native_hash_threads` (default 3) pre-hashes last frame's small buffers and
  textures on worker threads; render-thread record −0.6 ms at ~2,000 draws.
- **renderer.cpp:** control-file polling (`dump_request`, `menu_toggle`, `menu_set`, `diag_mode`)
  moved to a background thread. On the USB disk `exists()` blocked the render thread
  100–450 ms every 64 frames; worst moving frame fell from 490 ms to 36–61 ms.
  `HaveShader` answers from memory.
- **shader_compiler.cpp:** `OnShaderCreated` only queues the shader; disk I/O happens on an
  intake thread, not the game threads.
- **SDK (in the patch): `audio_release_pacing`** (default on for Windows). WASAPI ignores the
  256-sample hint and runs 10 ms periods, so audio passes arrived in pairs. A
  waitable-timer pacer now releases one pass every 5.33 ms.

## Results (hidden, uncapped, muted)
| Scenario | Fps |
|---|---|
| Standing, ~1,400–1,800 draws | 115–119 |
| Standing, ~2,200–2,500 draws | 87–95 |
| Running around | 68–93 (scene dependent) |
| First Windows run, for reference | 72–82 |

`gpu_skip_rendering_lag_ms` 2 vs 6 gives 96 vs 87 fps. The default stays 6 because 6 fixes
foliage flicker; the Esc-menu "Max performance" / "Low GPU latency" option uses 2.

## Where the time goes
- **Game thread:** mostly GTA code; the hottest function is the script VM (sub_83525E00).
- **Render thread:** the limit in heavy scenes (3,400–3,800 draws), about 6 µs per draw.
  At 3,832 draws: textures 2.5, streams 2.3, bind+draw 3.8, setup 1.0, consts 1.0 ms.
  The NVIDIA driver is ~15% of this thread. The next big lever is parallel command recording.
- **Audio:** the FFmpeg XMA decoder re-inits per stream (~5% of a core). Left alone on purpose
  (audio fidelity).
- **Remaining ≥90 ms hitches:** loading screens and first-time pipeline/texture creation. Game
  data on an SSD would help streaming.

## Tools (`tools/native`)
- `bench.ps1 NAME [-Secs] [-Still] [-Profile]`: hidden run, CPU per thread, timing lines.
- `ab.ps1 NAME SETTING A B [-Move]`: flips a cvar during one run and reports fps per value.
- `sample.py`: RIP sampler with PDB/export symbols.
- `bench_summary.sh LOG`: average fps, worst frame and hitch counts.

## Evening: owner play-test fixes
- **Loud audio bursts.** The Linux vexptefp codegen fix was missing because the game build ran
  the installed `rexgluerd.exe` from 10-23. The new tool is now installed and the code was
  regenerated with `--ignore-stamp` (292 files changed). Loudest peak in a hidden capture:
  -17.6 dBFS (the bursts were -3).
- **Foliage popping.**
  - `gtav_native_hash_threads` now defaults to 0: a hash taken at the start of the frame missed
    rewrites made before the draw.
  - An `ab.ps1` lag A/B had saved `gpu_skip_rendering_lag_ms=2` into `build/gtav_settings.ini`,
    which overrides `play.cmd`'s 6. The line is removed, and `ab.ps1` now restores the ini
    after a run.
- **Next: headless perf session for 100 fps.** Measure with `play.cmd`'s flags
  (`--gtav_audio_unsync=false`). The write watch is Linux-only, so Windows hashes every frame;
  porting it (MEM_WRITE_WATCH / GetWriteWatch) is the first lead.
- **Tree orbs / foliage vanishing up close.** The Linux `xenosrecomp-gtav.patch` (conditional-exec
  fix and dither order) had never been applied on Windows. The patch is now applied,
  XenosRecomp rebuilt, and all 3,617 shaders regenerated (`build/census7`, 2,327 changed) and
  installed in `build/spirv` (old set in `build/spirv_before_linuxxr`). Owner: "trees look good".
- **Re-measure the perf baseline before optimizing:** the numbers above predate the new shaders
  and code.

## Night: headless perf session (2026-10-29 ~18:00-19:10)
Same machine, hidden runs (`bench.ps1`, run-around input unless `-Still`).

| Run | Moving fps | Render record ms |
|---|---|---|
| Owner's `play.cmd` flags (`--gtav_audio_unsync=false`) | 62.5 (hard cap) | 8.6 |
| Audio unsynced (the default, as on Linux) | 88-100 | 8.6 |
| + Windows write watch | **96.6 avg** (60 s) | 6.4 |
| + lag 4 instead of 6 (in-run A/B) | 97.6 vs 88.3 at 6 | |

- **`play.cmd` no longer passes `--gtav_audio_unsync=false`.** That flag makes the update thread
  wait for every 16 ms audio frame (62.5 fps cap). It was a 10-27 experiment for the loud bursts and
  did not help; the bursts were the stale codegen (fixed 10-29).
- **Write watch on Windows** (`write_watch.cpp`, `_WIN32`): uses the SDK's physical write watch
  (`RegisterPhysicalMemoryInvalidationCallback` + `EnablePhysicalMemoryAccessCallbacks`: pages
  read-only in the A0/C0/E0 views, the SDK's exception handler reports the first write; file reads
  into watched pages are reported by the SDK's NtReadFile). A bitmap of armed pages keeps Watch off
  the SDK lock for armed pages; the epoch is taken before arming. Pages below 16 MB (the 7F window
  aliases them unwatched) are never trusted. ~10-20 faults a frame; full hashes 717 -> ~12 a frame;
  render thread record 8.6 -> 6.4 ms. `--gtav_native_write_watch_verify=true` for 60 s: 0 changes
  missed. Off with `--gtav_native_write_watch=false`.
- **Esc menu "Low GPU latency" now sets lag 4** (was 2, which flickered). 4 vs 6: +10% fps; in a
  headless `--gtav_native_flicker_catch` A/B lag 4 had 4 events vs 12 at 6 (lag 2: 16 vs 12, so
  the catcher is noisy and can't prove it). Default stays 6 until the owner checks driving.
- `ab.ps1` now also counts flicker-catch events per value.
- Tried and dropped: pinning the hot threads to P-cores (solo cores 70 fps vs 90 unpinned; P-core
  set ~86): Windows' scheduler is better here.
- Trap: passing `--gpu_skip_rendering_lag_ms` on the bench command line duplicates play.ps1's flag
  and the GPU plugin never loads ("no GPU emulation loaded", boot hang). Use ab.ps1 (menu_set).

### Where the time goes now
- Render thread ~6.4 ms/frame at ~2,000-2,600 draws (idle ~3.8 ms): not the limit.
- Main XThread and the D3D thread (F800092C) are each ~85-90% busy and hand frames to each other.
  Main: ~7% spinning in the GPU fence wait (8380CEF8 / 83807DC8; lag-bound), the rest game code
  (script VM 83525E00, ...). D3D thread: our draw capture (RecordDraw, constant compare/copy) is
  ~10% (~1 ms/frame), the rest D3D/game code.
- Standing at the spawn view (~2,550 draws): 82 fps, game-thread bound.
- Command processor thread ~15% busy.
- Next levers: lag 4 as default once driving looks clean; cheaper capture (skip the fetch
  compare when it usually differs: 85% of fetch blocks are copied anyway); PGO ~3% (dropped on
  Linux); streaming hitches (4 a minute, ~190 ms) = game data on the USB HDD.
- Loop iteration 2 (critical path = Main XThread; the D3D thread waits ~29% on it):
  - GTA's fence wait (8380CEF8) busy-spins (83807DC8, a ctr delay + a hang check; no sleeps),
    so its cost is how long the command processor takes to work through the kicks held by the
    lag (~0.9 ms/frame at lag 6, 9.4% of main). Only a smaller lag cuts it.
  - `--gtav_wait_profile` now also times SleepEx (8378F5D0, the only caller of
    KeDelayExecutionThread). Main calls it ~1,500x a frame from sub_83587B50 (via 827EFB60): a
    spin on a flag another thread clears, exponential backoff to 1024 then Sleep(0) = yield.
    2.4% of main; real waiting, not waste.
  - So the main thread is ~83% GTA code + ~10% fence wait + ~3% spin waits. Remaining levers:
    lag 4 (owner driving check), codegen-level (inline __savegprlr/__restgprlr ~2.5%, PGO ~3%).
