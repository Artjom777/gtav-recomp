# Next session: fix the remaining visual glitches and the sound (written 2026-10-28)

Owner's goal: push through every visual glitch and the audio. State at the end of the 10-27/28
session, in priority order, with the evidence and the next concrete step for each. Details of the
investigations: `docs/HANDOFF_LINUX_2026-10-27.md` (bottom sections) and
`docs/TREE_ORBS_INVESTIGATION.md`.

## Fixed this session (committed)
- Tree orbs: XenosRecomp ignored conditional exec (cexec bN / cexec p0); fixed in
  `xenosrecomp-gtav.patch`, all SPIR-V regenerated (392 shaders changed). Owner confirmed.
- "All textures changing" on long drives: the 32768-entry texture descriptor heap filled up and
  overflowed into the null slot. LRU eviction + an overflow slot (`backend.cpp` EvictTextures).
- Blocky shadows, mostly: G-buffer RT2 (EDRAM 600) gets alpha 1 at the G-buffer clear
  (`GTAV_NATIVE_GBUF_ALPHA1`), and float24 depth copies are rounded to 20e4
  (`GTAV_NATIVE_DEPTH_FLOAT24`). Tile data vs emulator: 40% off -> 8%.
- vmaxfp/vminfp propagate NaN like AltiVec (codegen; `rexglue-sdk-gtav-linux.patch`).
- Linux crash reporter (`crash_handler.cpp`: SIGABRT/terminate -> `build/gtav/crash.txt`).
- Disk: dumps filled the SSD once (57 GB). Per-draw snapshots are ~2.7 MB each; keep
  `--gtav_native_dump_rt_count` <= 700 and press F9 once; clean `build/gtav/logs/native_dump`.

## 1. Water (ocean rises / "underwater" look / one-frame grey plane over the sky)
- The ocean wave simulation (128x128 R32F heightfield, EDRAM base 000 fmt 14; ping-pong pairs
  0C087000/0C097000 and 0C088000/0C098000; passes ps 152069AE, 465068D2, B558693D, 294E97DB,
  BCFF0D3F, then ps 662F06BD -> 1x1 0473C000) is UNSTABLE natively: range +-2 in the emulator,
  natively +-5 after a minute, +-50..+-2000 later (also at GTAV_FPS=30, so not frame rate).
  Shadow-mode dumps stay bounded only because the native step reads the emulator's state
  (guest memory) each frame.
- One native step from the emulator's own state (shadow dump frame_10394 vs emu 10391) already
  differs: corr 0.6, native mean +0.26 vs -0.01, flat plateaus where the emulator has detail.
  In "on" mode passes 2-3 change almost nothing (shadow mode: pass 3 changes every texel).
  Update constants (UpdateParams0/1, UpdateOffset c224-c227) match.
- Next: recompute pass 1 (ps 152069AE HLSL) on the CPU from a dump's inputs (s13/s14 state raw,
  s11 noise, constants from draws.bin) and compare with both outputs -> find the wrong
  instruction/input. Check the VS outputs feeding r0.z (pass 3 amplitude) and the rect-list
  interpolators (native_rectlist.hlsl extrapolates the 4th corner's attributes).

## 2. Sound (harsh/reverby, loud bursts)
- Constant 16 kHz whine: the 15.8-16.2 kHz band carries as much energy as 200-8000 Hz in the
  final mix (`--audio_dump_raw`). Second-difference residual by sample phase mod 3 is
  2.0e-2 / 1.3e-2 / 1.1e-2 (mod 2 and 4 flat) -> ONE of the three output phases of a
  32 -> 48 kHz (2:3) conversion is computed wrong (sidebands at 16 kHz +- f).
- XMA output is clean (`--xma_dump_raw`, new SDK cvar): streams are 24/32/44.1/48 kHz.
- Audio engine thread = guest "XThreadC7FFF6C0" (stack samples: `hangdump.sh`), busy in
  sub_831C04D0 (scalar float linear-interp resampler: faithful translation checked), sub_83210BD0
  and sub_831C6130 (VMX), sub_831BE0A0. Checked and correct: vmaxfp/vminfp (now NaN-exact),
  vcfsx/vcfux/vctsxs/vctuxs (+128 aliases), vrfi*, vupkd3d128, lvlx/lvrx/stvlx/stvrx, fctidz,
  fcfid, fsel, frsp, fadds/fsubs (single rounding).
- Next: find who converts 32 kHz voices (trace a 32 kHz XMA context's output buffer into the
  mixer: watch the PCM addresses from the XMA context data, then the readers), or diff the
  mod-3 phase error per voice. Check sub_83210BD0 / sub_831C6130 VMX translations
  (vperm/vsldoi/vrlimi/vpermwi with immediates).

## 3. Blocky reflections / remaining shadow blocks (top band)
- Remaining tile errors sit in the top band: EDRAM leftovers of other surfaces. Proper fix:
  model EDRAM ownership like Xenia's render target cache (copy in overlapping ranges written
  through other targets/clears when a target is bound); TileAlias is the one hand-coded case.

## 4. Streaky terrain/road textures (countryside)
- Already in the G-buffer (normals RT). Terrain VS 6E5A219F / PS A497EF58 (parallax loop).
  Next: per-draw RT0/RT1 snapshots at a streak spot, A/B with parallax forced off.

## Tools (all off by default)
- F11 stage-isolation modes + GTAV_NATIVE_DIAG_PS/VS (ALL); GTAV_NATIVE_DUMP_ALPHA=1 (alpha view
  of per-draw snapshots); per-draw snapshots now match any of the draw's targets.
- `--gtav_native_pm4_scan`, `--gtav_native_dump_on_collapse`, `--gtav_native_flicker_catch`
  (one-frame glitch catcher -> logs/flicker), lost-draw log per frame.
- `tools/native/compare_raw.py` (decodes 24-bit depth formats 22/23 now),
  `compare_leaf_scale.py`, `--xma_dump_raw`, `hangdump.sh` for stack sampling.
- Unattended captures: `--gtav_native_hidden=true --audio_mute=true`, `touch
  build/gtav/logs/dump_request`; shadow mode pairs native + emulator dumps (logs/emu_dump).
  The spawn (Vespucci beach) does not run the water sim unless the camera is at the shore.
