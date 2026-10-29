# Next session: audio glitches, headless (written 2026-10-28 evening)

## RESULT (10-28 night, headless): loud mono bursts FIXED - codegen vexptefp out of range
- Cause: the vexptefp builder (rexglue-sdk src/codegen/builders/vector.cpp) built 2^n by writing
  n+127 into the exponent bits with no range handling: 2^-200 came out as -7e16, 2^-inf as NaN.
  The game's audio computes pow(x, y) = vexptefp(y * vlogefp(x)) for per-speaker gains; for a
  speaker lane that should be silent (x ~ 0) that gave a huge gain, clamped to the maximum, so a
  sound played at full level on the speakers it should have been silent on (FR/BL/BR identical,
  instant, while the real lane ramped in). Fix: 0 below -126 (non-Java flush), +inf from 128,
  NaN passes. 63 call sites regenerated (`rexglue codegen gtav_manifest.toml --ignore-stamp`).
- A/B with the same scripted gameplay: before 7 burst events (max -3.0 dBFS, 4 speakers equal),
  after 0 (max -14.5 dBFS). Runs: logs/aboot2_* (before), logs/aboot3_* (after).
- The "16 kHz whine" was an analysis artifact: the endpoint here is 6 ch, so --audio_dump_raw is
  6 ch interleaved; reading it as stereo makes a period-3 pattern (48k/3 = 16 kHz). Real mix: no
  16 kHz line. audio_marks.py now reads the channel count from the log.
- New tools: `tools/native/audio_boot_capture.sh NAME [SECS]` (hidden muted boot, mix + XMA dumps;
  GTAV_INPUT_SCRIPT=tools/native/input_run_around.txt plays a scripted pad once in the world),
  `tools/native/audio_bursts.py logs/NAME.log`. SDK: --input_script (scripted pad for user 0,
  also reports a pad as connected), --xma_dump_raw frames carry steady-clock ms, the mix dump logs
  "audio dump starts at steady ms N".
- Still open: owner play-test; Wasted screen delay (maybe related, untested).

Owner's instruction: work on the audio glitches **headlessly** (hidden, unattended runs are fine
for audio). Water is done; see "Fixed 10-28" below.

## Audio: what is known (details in NEXT_SESSION_2026-10-28.md section 2 and
## HANDOFF_LINUX_2026-10-27.md "Audio")
1. **Constant 16 kHz whine.** The 15.8-16.2 kHz band carries as much energy as 200-8000 Hz in
   the final mix (`--audio_dump_raw`). Second-difference residual by sample phase mod 3 is
   2.0e-2 / 1.3e-2 / 1.1e-2 (mod 2 and 4 flat): one of the three output phases of a 32 -> 48 kHz
   (2:3) conversion is computed wrong. XMA output is clean (`--xma_dump_raw`).
2. **Loud bursts.** Tonal (220-290 Hz), perfectly mono, 13-20 dB too loud: a voice that lost its
   distance attenuation / panning, or one XMA voice replayed wrongly.
3. Possibly linked: Wasted / mission-failed screen appears late.

## Plan (headless)
- Unattended capture: `./play.sh NAME --gtav_native_hidden=true --audio_dump_raw=PATH
  --xma_dump_raw=DIR` (audio NOT muted for the dump; check how audio_capture.sh does it), then
  `tools/native/audio_levels.py`, spectrum + mod-3 residual in Python. The spawn has ambience;
  enough for the whine.
- Find the 2:3 converter: the audio engine thread is guest "XThreadC7FFF6C0"; stack samples with
  `hangdump.sh` showed sub_831C04D0 (scalar resampler, checked), sub_83210BD0 (VMX, 370 lines:
  lvlx128/vspltw128/vrlimi128/vperm128 - vrlimi/vspltw lane maps checked OK 10-28),
  sub_831C6130, sub_831BE0A0. Next checks: VectorMaskL for unaligned lvlx at the offsets used,
  simde_mm_perm_epi8_, and the loop bookkeeping that emits 3 outputs per 2 inputs.
- If reading does not find it: a small hook (pattern of gtav/src/cutscene_audio_fix.cpp) that
  logs inputs/outputs of the candidates for one 32 kHz voice in one hidden run, then compare
  with an ideal 2:3 interpolation in Python to see which phase is wrong.
- Bursts: per-context XMA peak/RMS logging in rexglue-sdk src/audio/xma_context.cpp
  (decoder vs guest mixer gain/pan); then vrefp/vrsqrtefp/saturation in the pan code.
- Keep each hidden run purposeful (one line on what it decides); batch captures.

## Fixed 10-28 (pushed)
- 2183076 ocean wave simulation: tiled resolve fetched linear (LinearViewOfTiled); fetch
  rounding epsilon in XenosRecomp (all SPIR-V regenerated, census6); consecutive-frame dumps
  (`--gtav_native_dump_frames`); exact CPU refs tools/native/ocean_pass1.py / ocean_pass5.py.
- 0143ad2 water pre-pass EDRAM aliasing 4x MSAA <-> 1x (MsaaAlias + learned redirects, colour
  658 / depth 710), pipelines take the attachments' sample count, alias framebuffer lifetime
  (fixed a device-lost freeze).
- 95617bd guest polygon offset applied as dynamic depth bias (ocean troughs, shadow casters).

## Other open items
- Rain is not shown natively. No captured shader uses memexport, so first get an F9 during rain
  and find the rain draws.
- Blocky reflections / top-band shadow blocks (EDRAM ownership model), streaky terrain
  (parallax loop A497EF58) - see NEXT_SESSION_2026-10-28.md 3/4. Re-check both after the
  polygon-offset fix.

## NEXT SESSION (owner, 10-28 night): blocky reflections on cars and such
- Audio fix is committed; owner is play-testing it.
- Start from NEXT_SESSION_2026-10-28.md section 3: the remaining tile errors look like EDRAM
  leftovers of other surfaces; the proper fix is modelling EDRAM ownership like Xenia's render
  target cache (TileAlias / MsaaAlias are the hand-coded cases so far). Car reflections come from
  the reflection probe / cube resolves; re-check them after the 95617bd polygon-offset fix. Get an
  F9 dump at a car with blocky reflections and compare with the emulator (shadow mode).
- Owner (10-28 night, during the audio play-test): the SHADOWS are also still blocky. Treat both
  in the same session: the remaining shadow blocks (top band) were already suspected to share
  the EDRAM-leftover cause with the reflections (NEXT_SESSION_2026-10-28.md section 3). Get F9
  dumps of both (a car with blocky reflections, a spot with blocky shadows) in one sitting.

## 10-28 late night: blocky shadows + car reflections FIXED (214b060, pushed); Esc settings menu
- Cause (one bug): PS D258EBE3 builds the 80x45 sun-tile table, w = max of the 2x2 shadow-mask
  alphas (06AA0000.w) + 64 when the four differ exactly (penumbra flag), x not-all-sky. Tile VS
  variants pick by w >= tiledPenumbraOffsetValue (64). Native PCF differs from the Xbox by ~1/255,
  which flipped the flag (frame_9938: 697 tiles flagged on the emulator, not natively), and those
  tiles took the cheap uniform variant = 16x16 blocks. Reflection maps (cube 065B2000 -> paraboloid
  069C2000) match the emulator. Shadow-mode pair: native_dump/frame_9938 + logs/emu_dump/frame_9939.
- Fix: gtav/src/native/shaders/ps_D258EBE32001470C_tiles.hlsl (census6 output, flag always set),
  embedded at build time; --gtav_native_tile_penumbra_all (default true, live from the menu).
  Owner: "shadows and reflections both look good, fps feels good".
- Esc menu (native window): gtav/src/native/menu.cpp (Dear ImGui from librexruntime.so), drawn by
  Backend::DrawMenu (shaders/native_menu.hlsl, vertices by device address). Presets Best image /
  Max performance, fps limit, VSync (live), FPS counter, per-pixel sun shadows, background pipeline
  compiles. Saved to build/gtav/gtav_settings.ini, applied over the command line at start.
  Test hooks: touch build/gtav/logs/menu_toggle (= Esc), echo name=value > logs/menu_set.
- Menu freeze fixed in the SDK: the SDK's ImGuiDrawer switches the process-wide GImGui on the UI
  thread for every mouse event; with the menu on the render thread both swapped contexts and the
  render thread hung (log signature "renderer state 1"). thirdparty/imgui_rex_config.h makes the
  context thread_local (IMGUI_USER_CONFIG on the imgui target). In rexglue-sdk-gtav-linux.patch;
  the Windows patch (rexglue-sdk-gtav-windows.patch, already different) still needs it.
- Not yet measured: the fps cost of the per-pixel tiles (toggle it in the menu outdoors).

## NEXT SESSION (owner, 10-28 late night): flashing / flicker while driving fast
- Owner: when driving fast the screen flickers, "big things in the sky appearing", maybe loading
  artifacts. Last major visual glitch they see; goal is to get rid of the flashing.
- Tools already there: --gtav_native_flicker_catch=true detects one-frame glitches (a frame unlike
  both neighbours), saves them to logs/flicker and logs them; F10 screenshots; F9 dumps (shadow mode
  pairs with the emulator, keep dump_rt_count <= 700, one F9).
- Leads to check first: (1) draws skipped while pipelines/shaders compile ("frame N lost draws" log
  lines; menu toggle "Compile new effects in the background" off -> does the flashing stop?);
  (2) streamed LOD/terrain meshes drawn with stale or half-streamed vertex data (sampled-hash buffer
  cache, --gtav_native_exact_cache=true A/B); (3) stale resolves (ResolveCurrent) or textures
  evicted/recycled while streaming; (4) does the emulator renderer (GTAV_NATIVE=off or shadow) show it?

## DONE 10-29 ~00:50: driving flicker fixed (owner: "i dont see any flickering"), pushed e4aac2f
- Sky slabs / stray meshes: the command processor wrote mid-frame GPU fences (kick sub_8380D8A8
  writes dev+11036 to *(dev+11024) = phys 1F5C0040, then +2) before the renderer recorded the
  frame; the game recycled the memory. SDK hook rex_skip_rendering_fence (EVENT_WRITE_SHD) +
  fence-time vertex/index copies in capture.cpp (--gtav_native_fence_copy); the command processor
  waits while that frame is being recorded.
- Car vanishing: background pipeline compiles skipped draws; they now wait up to
  --gtav_native_pipeline_wait_ms (40) per frame.
- Foliage flicker: the command processor ran right behind the CPU, so copies caught buffers the
  game fills after the kick. SDK --gpu_skip_rendering_lag_ms (play.sh / play.ps1 pass 6).
- Dead ends: copying at the D3D call (flicker lock on cars), --gtav_native_fence_wait (deadlocks
  loading, costs fps), holding back only the fence value (the game uses other progress signals).
- Tools: F8 = last 90 frames + counters (--gtav_native_replay=true, logs/replay/<frame>/);
  --gtav_native_flicker_catch saves before/glitch/after PPM + per-draw lists (first 40).
- Not yet: Windows SDK patch lacks the new hooks; perf (owner: later).

## NEXT SESSION (owner, 10-29): cell phone transparency
- A weird semi-transparent box around the in-game cell phone. Older note: "phone black box on
  host-RT path (fine with --render_target_path_vulkan=fsi)" was the emulator path; check what the
  native renderer does with the phone's render target / its alpha (F9 dump with the phone up).

## DONE 10-29 morning: cell phone box fixed (not yet committed; owner to play-test)
- The phone pass saves the scene behind the phone (resolve 920,376-1280,720 -> 05ADA000), clears
  2D0 there, draws the phone as 8888, then puts the scene back and blends the phone over it with
  full-screen rects. On the Xbox those are clipped: Clear gets a D3DRECT (920,376-1280,720) and the
  draws a window scissor. Natively both were ignored, so the clear wiped all of 2D0 (the full-screen
  put-back hid that with stale texels) and the phone's edge filter blended the scene copy left in
  the phone layer over a band around it = the semi-transparent box.
- Fix: d3d_hooks.cpp passes Clear's rects (r4 count, r5 rects) -> ClearCommand.rects; backend Clear
  clears only those rects; draws and clears use the guest window scissor
  (--gtav_native_scissor, default on). D3D keeps PA_SC_WINDOW_SCISSOR_TL/BR as dwords 17/18 of its
  0x2000 register block (sub_83806BE0 writes them), read as regs::D3D_WINDOW_SCISSOR_TL/BR.
- Inside the box the game's own UVs put the scene back one texel off (u = x/W + 1/W, D3D9 pixel
  centres); left as is (same maths on hardware), invisible.
- Dump additions: "rect scissor ... verts" lines for small rect lists, scissor + "clear rect" on clears.
- Hidden phone capture: scratchpad phone_run.sh style = play.sh hidden + --input_script with D-pad up
  (buttons 0001) every 2 s, touch logs/dump_request ~30 s later. Pressed right after spawn, the phone
  doesn't open.
- Owner play-tested 10-29 09:15: "that works, the game is looking good".

## NEXT SESSION (owner, 10-29): performance pass, goal >= 100 fps on this laptop
- Big planned ("ultra thinking") session: make the native renderer as fast as possible.
- Baseline (logs/play_phonefix.log, beach, ~3,400 draws/frame): CPU record ~30 ms/frame =
  setup 11 + textures 6.5 (810 full texture hashes, 12 MB, 3 ms) + streams 4.5 + bind+draw 4.3 +
  consts 3 + pipeline 0.5. Earlier boot tests: ~57-68 fps.
- Costs added for correctness, to re-examine without regressing: fence-time vertex/index copies,
  pipeline wait (40 ms), command processor lag (6 ms), per-pixel sun tiles, texture hashing.
- After each change: visual A/B (flicker while driving, phone, shadows, water) plus the
  "record ms per frame" log lines.

## DONE 10-29 afternoon: performance pass (headless), ~37-48 fps -> ~100-117 fps (hidden benches)
Local commits ca3622e..a58ec9c (not pushed: owner to play-test first). Numbers are hidden runs
on this laptop (RTX 4050, "on" mode, uncapped, --gtav_native_vsync=false): moving around
(tools/native/input_run_around.txt) ~96-107 fps at 1,600-2,600 draws; standing at spawn ~117.
Heavy scenes (the beach, ~3,400 draws) were not reachable headlessly.

What gated the frame, in the order found:
- Render thread: a diagnostic hash of every draw's indices (now --gtav_native_check_draw_data),
  per-frame hashing of textures/buffers (now the kernel tracks guest writes: write_watch.cpp,
  userfaultfd async write-protect + PAGEMAP_SCAN; only pages copies were made from are
  protected; the four guest views are scanned in parallel; --gtav_native_write_watch_verify
  re-hashes "unchanged" copies and counts misses: 0), full 4 KB constant swaps (only the register
  runs each shader reads now, from xenia's ucode analysis: SDK rex_shader_float_constant_bitmap),
  vertex layout rebuilt per draw. Record 19-23 ms -> ~7 ms at ~1,800 draws.
- Game render thread (capture): 10 KB of constant copies per draw (now only the used runs).
- Command processor: GetRegisterInfo's switch per register write, one virtual call per
  register (type-0 packets are range writes now), shader loads under gpu_skip_rendering.
- Update thread (the bottleneck now): SDL's ~1024-sample audio period made the guest's audio
  engine run four 5.3 ms passes in a burst every 21 ms, and the update thread waits for a pass
  every frame (SDK --audio_device_frames=256). WaitMultiple polled in 1 ms steps (futex now,
  per-handle). GPU fence waits took the whole 6 ms command processor lag (kicks are let through
  while the game blocks on a fence: gpu_fence_wait.cpp + SDK rex_skip_rendering_gpu_waiters /
  rex_skip_rendering_wake). Game code built with -mcmodel=small -march=x86-64-v3.

Owner checks (play-test, then push):
- Driving flicker, phone box, shadows/car reflections, water: unchanged visuals expected.
- Audio: the device period is now 256 samples (5.3 ms); listen for crackles (none in logs).
- Esc menu "Low GPU latency" (command processor 2 ms behind instead of 6): +23% fps in an A/B
  run; OFF by default because 6 ms is what fixed the foliage flicker. Try it while driving.
- build/gtav/gtav_settings.ini has gtav_native_vsync=true: fps capped at the display refresh.

Tools: tools/native/bench.sh NAME SECS [flags] (hidden timing run; BENCH_PERF=1 perf profile,
needs kernel.perf_event_paranoid=1; BENCH_INPUT=none stands still), tools/native/ab.sh NAME
SETTING A B (flips a runtime setting during ONE run: separate runs see different scenes and
misled two comparisons), --gtav_wait_profile (per-thread waits by caller, who releases the
semaphores, audio/update timeline, GPU fence distances). Tried and dropped: SDK mprotect write
watch (250 ms stalls), protecting a copy's pages at each recheck (missed writes), audio command
stamp hook (no effect), command processor lag 0/3 as default, PGO (GTAV_PGO, ~3%, off).
Next levers: the update thread is game code (~8 ms/frame); our renderer ~4 us/draw (parallel
recording would help heavy scenes); the SDK global critical-region mutex is contended
(object table lookups) - ~6% kernel time on the update thread.

## NEXT SESSION (owner, 10-29): the Android port, working well
Owner play-tested the performance pass ("that looks great"); pushed d10cd61 with
docs/HANDOFF_WINDOWS_MAC_2026-10-29.md. Next on this machine: get GTA V running well on Android.
Starting facts:
- Nothing Android exists yet: no NDK/SDK on this machine, no Android build of the SDK or game.
  The SDK has a few REX_PLATFORM_ANDROID paths from xenia (memory_posix, mapped_memory_posix,
  threading_posix, Vulkan instance/presenter).
- arm64: the x86 intrinsics in gtav/src/native/backend.cpp (SSSE3 byte swaps) need NEON/scalar
  paths; -mcmodel/-march block in gtav/CMakeLists.txt is x86 only; the generated code is
  portable C++ (check the SDK's PPC intrinsics header for x86-only bits).
- Write watch (write_watch.cpp) needs userfaultfd + PAGEMAP_SCAN (kernel 6.7+); Android apps are
  normally blocked from userfaultfd, so expect the hashing fallback (costlier on a phone CPU).
- The 5.3 ms audio pass cadence matters (update thread waits for it): check the `[waits]
  timeline` on the device (AAudio period).
- Memory: the guest arena is a 4.8 GB shared mapping (+ views); phones need a smaller/differently
  backed arena or a 64-bit address space with overcommit; game data (~8 GB) must live on device.
- Plan the session first (toolchain, build, packaging, input, data location, perf targets).
