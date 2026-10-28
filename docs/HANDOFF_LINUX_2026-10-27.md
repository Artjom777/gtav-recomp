# GTA V native renderer: Windows → Linux handoff (2026-10-27)

Written on Windows at the end of the overnight session (2026-10-26 evening → 10-27 ~10:40).
The Windows tree is `E:\gtav-native` (git, branch `master`, no remote; HEAD `74ec30c`).
**All work below is uncommitted** in that tree; the patches in its root carry the submodule
and SDK changes. Read `docs/HANDOFF_WINDOWS_MAC.md` for the architecture and pins, and
`NEXT_SESSION_PLAN.md` for the older chronological status.

Rule that still applies: never use the leaked Rockstar source, not even for names.

## State in one paragraph

The native renderer runs the game outdoors at ~80-100 fps (hidden-window measurements at the
save spot; owner played it 10-27 morning). Renderer CPU record time per frame went from ~18 ms
to ~11 ms. The 2-s freezes every 20 s and the shader-miss stalls are gone. Foliage uses the
Xenos alpha-to-mask dither, and the G-buffer tile-alias fix cured the blocky shadows / car
squares in the frames it was derived from. **Open, owner-reported 10-27:** loud audio bursts
while driving / with many sounds (ReXGlue side, see below), the Wasted message appearing
late, glowing halos and opaque dark water at the ocean, and blocky shadows in some places
(airport tarmac, a "$" sign). The 10:28 audio-capture run had no Wasted screen; whether the
audio-sync setting changed its timing was not reported.

## What changed (Windows, 10-26 evening → 10-27)

All in `gtav/src/native/` unless noted.

### Performance
- **Never read or scatter-write upload memory** (write-combined): shared constants are built in
  a local array and copied once (`shared_cpu` in `backend.cpp` Draw), vertex/index conversions go
  through `ConvertInto` (a scratch buffer + one memcpy). The fetch-cache readback alone had cost
  ~100 ms/frame.
- `SlotMemo`: per-slot memo of TextureDescriptor + SamplerDescriptor keyed by the 6 fetch
  words (4096 direct-mapped, generation bumped at every Resolve and frame start). ~25k hits/frame.
- `ConstantUpload` (SSSE3 byte swap, reuse when the guest offset repeats), capture-side dedupe of
  const/fetch blocks (`ByteArena::Shrink`), `FetchCache`, `DrawState` redundant-bind skipping
  (`InvalidateDrawState()` wherever other code binds state), `kSmallTexture` 64 KB, sampled
  hashes for >64 KB buffers.
- Pipeline cache save (`renderer.cpp MaybeSavePipelineCache`): the 33 MB file is written by a
  detached thread. On a busy disk that write took 0.3-3 s on the renderer thread every 20 s,
  and the command processor's swap wait made the whole game freeze.
- `SpirvPreload` (`backend.cpp`): reads the SPIR-V folder at start-up, then 3 below-normal
  threads create every `vkCreateShaderModule` ahead of use (~17 ms each on the RTX 4050 driver;
  new areas used to freeze 1-2 s). `TakeShader` waits if a module is mid-creation;
  `KnownMissing` skips fopen of absent files / `.indirect` markers. Logs: "preloaded N shader
  files", "created N shader modules ahead of use".
- Occlusion queries: `OverrideQueryResult` (`capture.cpp`) returns the newest finished
  result; GTA re-issues the 25 corona queries before results arrive ("2100 a few frames old,
  0 not ready").
- Profiling lines every 300 frames: "record ms per frame: setup consts textures streams
  pipeline bind+draw", "textures per frame", "texture ms per frame: slots N, M slots from the
  memo". Remaining cost: textures ~3.5, streams ~2.5, bind+draw ~2.6, consts ~1.5 ms. The CP
  still waits ~8-10 ms per swap (renderer-bound); next lever = multithreaded recording.

### Rendering
- **Tile alias** (`NoteTileResolve` / `TileAlias`): the G-buffer is predicated-tiled (split at
  row 384) and RT2's bottom tile reaches the tile data 06AA0000 only through EDRAM aliasing
  into surface 480; natively those rows stayed 0, so the lower half used the wrong tiled-light
  variants (blocky shadows, black squares on car interiors). Log "[native] tile alias".
- **Alpha-to-mask dither** (XenosRecomp + backend): RB_COLORCONTROL alpha_to_mask at 1x MSAA
  is the Xenos 2x2 dither (offsets in bits 24-31, e.g. 87000014 → 3,1,0,2). Host A2C at one
  sample was a hard 0.5 cut (fading foliage popped on/off). Now shared constant
  `g_AlphaToMask` at byte 532 (`sc[133]` = offsets | 0x100) and the pixel shader clips
  `alpha < 1 - offset/4` by quad position (Xenia's 1-sample path); host A2C only with MSAA.
  Changes `shader_common.h` + `shader_recompiler.cpp`, so **regenerate all SPIR-V on Linux**
  (`tools/native/shader_census.py <shader_src> <out> -j N`, then copy `out/spirv/*` into the
  SPIR-V dir; 1944 pixel shaders contain the dither).

### Robustness
- Cold-boot hang (first launch after a rebuild sometimes stopped after "[native] using NVIDIA"):
  suspected `HideSdkWindows`' synchronous `ShowWindow` on the SDK main thread's SDL window
  blocking the native window thread while `createSwapChain` needed it. Now `ShowWindowAsync`;
  Init steps are logged ("[native] init: ..."). Several clean boots since; not proven.
  Windows-only code, but keep the pattern in mind for the Linux window thread.
- Swap-wait diagnostics: "[native] swap wait timed out: waiting for guest frame X, recorded
  through Y, last noted Z, N queued, renderer state S on frame F" (S 0 = waiting/init,
  1 = recording). In-game timeouts now only come with F9 dumps and module creation at load.

### SDK (`rexglue-sdk-gtav-windows.patch`, regenerated 10-27 04:27)
- `--audio_dump_raw=PATH`: writes the device mix (float32 interleaved, 48 kHz, silence
  included) from a writer thread (`RawDump` in `sdl_audio_driver.cpp`). Do not write files in
  the SDL callback: that stalled the device to 0.4x real time and faked a loud burst.
- The XMA changes in `src/audio/xma_context.cpp` (xenia-canary partial-frame fix + "starved on
  input" write_offset reset) were made earlier for the cutscene audio hang; they are suspects
  for the loud bursts (below).
- `rexglue-sdk-gtav-linux.patch` (10-26 14:05) does NOT have the audio dump; merge the Windows
  patch's `sdl_audio_driver.cpp` / `xma_context.cpp` hunks into the Linux tree.

## Open problems, with evidence

### 1. Loud audio bursts (ReXGlue audio path, not the renderer)
Owner: "weird loud noises every once in a while", "like me driving or if there are multiple
sounds". Capture 10-27 10:28 (`audio_capture.cmd`, F10 right after each noise):
- `E:\gtav-native\logs\audio_capture.raw` (float32 stereo 48 kHz), log `logs\boot\audio.log`,
  clips `logs\audio_capture_clip_0.wav` / `_clip_1.wav`, screenshots
  `build\logs\shots\native_009032.ppm` / `native_011021.ppm` (both driving in town; phone ringing
  in the first).
- `python tools/native/audio_marks.py logs/boot/audio.log logs/audio_capture.raw`:
  - mark 0: a ~0.4 s burst at -13 dBFS over a -33 dBFS bed (+20 dB), peak -0.9 dBFS;
  - mark 1: +13 dB, ~1.9 s before the key press.
- Both bursts are tonal (peaks 220-290 Hz), **perfectly mono (L/R correlation 1.00)**, smooth (no
  clicks, max sample step 0.06), so this is not decoder noise. A real sound is played
  13-20 dB too loud and centred: a voice that lost its distance attenuation or panning, or one
  XMA voice decoded/replayed wrongly.
- Ruled out: audio starvation (0 silent device frames); the audio-sync setting
  (`--gtav_audio_unsync=false` caps at ~62 fps; bursts remained).
- The native renderer never touches audio. The path is the recompiled game mixer plus ReXGlue's
  XMA/APU emulation.
- **Next step:** in `xma_context.cpp`, log per-context peak/RMS of each decoded block (context
  id, time, stream position) and capture again with F10 marks:
  - **XMA output loud at the burst:** decoder or our XMA changes (also try reverting the
    "starved on input" hunk first);
  - **XMA output normal:** the guest mixer computes a wrong gain/pan (recompiled VMX math such as
    vrefp/vrsqrte estimates or saturation).
- Cheap cross-check: does the emulator build (`E:\gtav-rex`, same ReXGlue audio) do it?

### 2. Wasted / Mission-failed message late
Owner: "wasted screen still takes a long time to show up message". During the 09:53 session the
renderer held 70-110 fps around the death (respawn loading at 10:08:28), and the game logged
nothing unusual. So the delay is on the game side, not a renderer stall. Untested ideas:
- the death sequence waits on game audio (stream/scene), which ties it to problem 1;
- something in the death screen is paced per frame or per vblank, and the uncapped frame
  rate / `kFrameIntervalVblanks` = 0 (`gtav/src/frame_limit.cpp`) changes its timing.
  Try `-Fps 30` (game's own limiter) once.

### 3. Ocean: glowing halos + opaque dark water
F9 dumps `build\logs\native_dump\frame_11410`, `frame_15964` (boat at the pier), `frame_17233`
(open ocean, flat dark brown).
- The water is not in the G-buffer (black there), and the lit result `084_06AA0000` looks fine.
- The halos (golden glow around boat/cars/shore) appear between resolve #113 and #260, i.e. in
  the water / forward pass.
- Prime suspect: the water shader's scene-depth read (thickness → shoreline foam / fog) gets
  wrong depth (float24 pack, `07568000` / `067CD000`), so everything counts as shallow.
- Per-draw snapshots ran out (budget) on 268 small additive draws (vs 1B4502E1 / ps FAB0D0FF)
  before the water draws. Raise the snapshot budget or start snapshots at resolve #113.
- Best method: `play_compare.cmd` at the beach, F9 in the "native (shadow)" window, then
  `compare_raw.py` for the first divergent resolve after #113.

### 4. Blocky shadows in some places
Frames `frame_62085` ("$" sign), `frame_69651` (airport tarmac). The tile alias fires (8 times in
the session) and 06AA0000 has no zero rows in 69651, so this is a different cause from the
fixed one. Shadow-map cascades 0526F000 / mask 06791000 chain: see NEXT_SESSION_PLAN.md.
Plane frames `frame_77408` / `frame_80983` show the older bright "wedges" over distant ground
(plan file `memoized-soaring-storm.md` on Windows, step 2 decision tree).

## Tools added this session (`tools/native/`, all uncommitted)
- `audio_levels.py DUMP.raw`: loudness per 100 ms, clipping, >12 dB jumps.
- `audio_marks.py LOG RAW [before] [after]`: cuts F10-marked moments out of a capture and writes
  WAVs; dump t0 = first "audio: ... frames" line - 10 s.
- `lit_steps.py DUMP [from] [to] [grid]`: per-draw snapshot changes; `side_by_side.py`.
- Root: `night_perf.sh NAME SECS [flags]` (hidden muted run, in-game = 4-digit draw count,
  greps timing), `night_compare.sh`, `audio_capture.cmd`, `build_sdk.cmd`.

## Windows-only notes (for the next Windows session)
- Build: `build.cmd 6` (copies to `gtav_fast.exe` unless running), SDK `build_sdk.cmd`
  (slow; copies the DLLs), XenosRecomp `build_xr.cmd`.
- E: is a busy exFAT USB disk: cold reads are slow (95-s shader preload when the cache is cold).
- `play.cmd` currently passes `--gtav_audio_unsync=false` (~62 fps cap). It was an experiment for
  the bursts and didn't fix them; restore uncapped by removing the flag.
- Another session ran `Skate.exe` overnight; don't run GTA beside a game someone else started.

## 10-27 night (Linux): blocky shadows/reflections traced (not fixed yet)
Dumps: frame_24735 / frame_25552 (owner, "on"), frame_7046 + logs/emu_dump/frame_7040-7047 (shadow pair).
- The blocks are 16x16 tiles: the tiled lighting pass draws 8 variants (rect lists, 10800 idx =
  80x45 tiles) and VS E43FC991.. picks a tile by its 80x45 tile data (058C7000, read via
  vfetchIndirect from ResolvedMemory, so the data is this frame's): `r1.y > 0.05` and
  `tiledPenumbraOffsetValue > r1.z` with r1.yz = tile.zw.
- tile.w (and every level of its 640x360 -> 80x45 chain, 065A2000) is the ONLY channel that
  differs from the emulator (tools/native/compare_raw.py; depth formats 22/23 now decoded).
- Producer chain of w: PS D258EBE3 (640x360) takes max of 06AA0000 (resolve #40) .w; #40's
  alpha = PS 487A191F (plain copy of 06AA0000 resolve #28) x shadow pass PS 5416149F (alpha only,
  blend dst*src.a; its inputs - cascades 0526F000, 1D lookup 0526C000, depth - all match).
- So resolve #28's alpha (surface 0x480 = G-buffer RT3 + RT2's bottom tile via the tile alias)
  differs: sky alpha 0 natively vs 1 in the emulator (nothing draws the sky; the Xbox value is
  whatever EDRAM held there), lit ground 1.0 vs 0.8.
- Root cause class: EDRAM aliasing. Native targets are separate textures per base/format/pitch;
  only TileAlias copies one case. Fix direction: model EDRAM ownership like Xenia's render
  target cache (when a target is bound, copy in the overlapping ranges last written through
  other targets/clears), or find the specific producer of 0x480's leftover alpha.
- 10-27 late: RT3-alpha experiment (GTAV_NATIVE_GBUF_RT3_ALPHA=1, backend Clear) did NOT remove the
  tile blocks (owner shots native_015104/015301): the sky alpha is not the main cause.

## Streaky terrain/road textures (owner, countryside road; dumps frame_15104/15301/15302)
- Already in the G-buffer (normals RT 026_05E72000 shows the horizontal smears), so G-buffer
  drawing, not lighting. Terrain = VS 6E5A219F / PS A497EF58 (182 draws). The cexec fix only
  added a redundant `if (p0)` around its parallax loop (identical semantics) -> not a regression
  from census5 as far as the HLSL shows. Suspect: the parallax-occlusion loop (i31 = 8 literal,
  r0.xy += r4.xy steps by parallaxScaleBias; height from s3.w) or the inputs it uses.
- Next: per-draw RT0/RT1 snapshots at a streak spot (--gtav_native_dump_rt=384), pick a terrain
  draw, and A/B with the parallax step forced off (e.g. GTAV_NATIVE_SKIP_PS on that PS variant, or
  a diag that zeros parallaxScaleBias).

## 10-27 late night: blocky shadows - two fixes in (backend.cpp / native_blit.hlsl)
1. G-buffer RT2 (EDRAM 600) leftover alpha: RT2 alpha is a 0/1 flag the screen-space shadow pass
   multiplies into (06AA0000) and the 80x45 tile classification reads. Where nothing draws (sky)
   the Xbox has 1, natively 0 -> wrong tile variants. Clear() now sets RT2 to (0,0,0,1) at the
   G-buffer clear (GTAV_NATIVE_GBUF_ALPHA1=<hex bases>, default 600, "none" disables).
2. Float24 depth: the Xbox stores D24FS8 depth as 20e4 (truncated); GTA's shadow bias is tuned to
   it. Depth copies of float24 surfaces (PSDepth, mode 2) now round to 20e4
   (GTAV_NATIVE_DEPTH_FLOAT24=0 disables). Debug shader variants of ps 5416149F showed the PCF term
   (not lookup/fade) was 0 on facades the emulator lit.
- Tile channel w vs emulator (80x45): 40% off -> 8% (frame_7238/emu 7239); mid/bottom exact,
  top band still 1.04 (leftover EDRAM content of other surfaces; full EDRAM model needed).

## Audio: 16 kHz whine = one broken phase of the game's 32->48 kHz resampler
- --xma_dump_raw (new SDK cvar): decoded XMA streams are 24/32/44.1/48 kHz and clean.
- Final mix (audio_dump_raw): the 15.8-16.2 kHz band carries as much energy as 200-8000 Hz;
  second-difference residual by sample phase mod 3 = 1.995e-2 / 1.29e-2 / 1.14e-2 (mod 2 and 4
  flat) -> a period-3 error = one of the three 2:3 output phases computed wrong (sidebands 16k +- f).
  The resampler is guest code (recompiled VMX); next: find it from audio-thread stacks
  (hangdump.sh) and check its generated code. Also done: vmaxfp/vminfp now return NaN like
  AltiVec (codegen; did not remove the whine).
