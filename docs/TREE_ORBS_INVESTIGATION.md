# Black tree "orbs" + foliage flicker (native renderer, Linux) — investigation notes, 2026-10-27

Symptom (owner): in native mode ("on" and "shadow"), trees show a dark/black octagon ("orb") and foliage
flickers / switches LOD as you move; trees look right at some distances and lose leaves when closer.
The emulator renderer (`GTAV_NATIVE=off`) shows the same places correctly. Frame rate (30 vs uncapped)
makes no difference.

## Established facts
- The orb is an octagon that is part of the tree model (e.g. vs 9B97485E/ps 2E65367E bark-textured
  trunk mesh n=2487, or vs 49CD0CC3/ps 6CF5AEFA n=144, DXT1 s0 with alpha swizzle ONE = opaque on Xbox
  too). On the Xbox the leaf cards cover it; natively the leaves are missing/sparse, so it shows.
- Side-by-side dump (shadow mode, same frame 3427 vs emu 3428): emulator albedo has a full canopy,
  native has the octagon and few leaves.
- Draw lists match: same pixel-shader ucode hashes and counts (emu runs every G-buffer draw twice =
  predicated tiling, whole pass per tile, so pair native draw k with the k-th emu draw of the FIRST
  half). With that pairing VS and PS constants match for 1130/1135 draws (c244+ are shader literals,
  always differ). So shaders + constants + draw counts are right.
- Emu ucode hash A4A965C189287B99 = "no pixel shader" (our hash 0); its 456 1-point draws are
  query/EDRAM helpers, not missing geometry.
- All G-buffer draws have RB_COLORCONTROL = AA000003: alpha test OFF, alpha-to-mask OFF. Leaf cutout
  is done in-shader (alpha * globalScalars.x vs gAlphaRefVec0 with HardAlphaBlend remap, screen-door
  checker from vPos, kill via literal compare) or by the texture alone.
- Leaf passes seen: ps D0B9D6DC writes only RT0 alpha (mask 0008, z test GREATER no write);
  colour pass ps 9AC27800 (z GEQUAL, write) has no kill; 39/45 of its draws changed no pixels.

## Ruled out (tested)
- Alpha test compare function (fixed anyway: a55d07a, g_AlphaFunc; GTA uses funcs 3,4,5,6, never 0).
- Dither index order (fixed: y low bit | x high bit).
- Tint palettes (s18 64x16 8_8_8_8): decode verified independently (4 identical row groups, entry 0
  black is real); orb draw's palette entry was a valid grey-green.
- Vertex colour channel order (R=natural AO, G=artificial AO, B=tint index; correct as BGRA).
- Vertex alpha (all FF on the trunk/octagon draws).
- All-zero / unstreamed textures (logging: none on canopy draws).
- Stale vertex/texture cache (--gtav_native_exact_cache=true: no change).
- Snapshotting vertex+index data at the D3D call (--gtav_native_snapshot_draws=true: no change),
  although ~10% of draws' exact vertex/index bytes differ between capture and record.
- Frame rate / frame-interval patch (GTAV_FPS=30 still broken).

## Open leads for the planning session
1. Per-draw pixel comparison against the emulator: the emulator does not dump per-draw images. Add an
   emu-side per-draw snapshot (or per-draw sample of a few pixels) for the leaf draws to see which draw
   produces leaves in the emu and not natively.
2. Textures read at record time vs emulator execution time (snapshot textures at capture like vertices).
3. Leaf vertex shaders: vfetchIndirect / computed-index fetch, instancing, wind/billboard VS math,
   `iFace`/vPos handling, half-pixel/NDC; check leaf draws' positions (are they off-screen / degenerate).
4. Depth: leaves fail GEQUAL? Compare depth buffer before the leaf pass (emu vs native, 07568000 /
   067CD000 float24 pack) and depth range/viewport z for those draws.
5. Predicated tiling: emu draws per tile with screen offsets; any per-tile state (window offset,
   scissor, PA_SC_WINDOW_OFFSET, resolve of the top tile) that leaves depend on.
6. Culling/winding (cull=00018002) for two-sided leaf cards.

## Tools added this session
- F9 dumps: per-draw snapshots of an EDRAM target (`--gtav_native_dump_rt=<decimal base>`, e.g. 384
  for the G-buffer RT0 0x180; `--gtav_native_dump_rt_count=1100`), `cc=` and `aref=` in draw lines,
  tint palettes `tex_*_f6_*.bin`, raw COLOR0 histograms for palette draws, `Z` = all-zero texture.
- Shadow mode F9 now also dumps the emulator side on Linux (Vulkan EmuDumpActive): logs/emu_dump/,
  compare with `~/.venvs/reskate/bin/python tools/native/compare_raw.py NATIVE_DUMP logs/emu_dump`
  and `side_by_side.py NATIVE_DUMP EMU_FRAME SLOT`. emu draws.bin = {prim,count,ps ucode hash,
  1024 VS + 1024 PS float constants} per draw, same as native draws.bin.
- Unattended runs: `--gtav_native_hidden=true --audio_mute=true`, `touch build/gtav/logs/dump_request`.
  Spawn point (Vespucci beach) has no LOD trees; orbs need the owner to drive.
- "[native] draw data rewritten after capture" stats every 300 frames.

## 2026-10-27 late: stage-isolation switch (plan ~/.claude/plans/zippy-scribbling-otter.md)
- F11 (or `echo N > build/gtav/logs/diag_mode`) cycles a diagnostic mode on the picked draws:
  0 off, 1 depth_always, 2 no_cull, 3 flat (magenta RT0 rgb, no textures/kill, keeps depth),
  4 flat+depth_always. Selection: `GTAV_NATIVE_DIAG_PS` / `GTAV_NATIVE_DIAG_VS=hash,hash`
  (default VS 49CD0CC3ED116BB1). Mode changes are logged as "[native] tree diagnostic mode".
- Read: flat+depth_always full canopy but flat sparse -> depth; flat full but normal sparse -> PS;
  flat+depth_always sparse -> geometry; no_cull fixes it -> winding.
- First hidden run (logs/shots_diag, the idle camera moved between modes, so no same-spot A/B):
  all modes work without errors; an orb was visible in mode off. VS 49CD0CC3 is also used by
  buildings, so the default selection is broad. The first build wrote RT0 alpha, and the lighting
  turned the magenta black; flat now writes RT0 rgb only.
- Facts behind the plan: tree colour passes are dc=00708767 (GEQUAL, write); the D0B9D6DC alpha
  pass comes after them (GREATER, no write). Host depth is always D32F_S8, and the VS position
  isn't `precise`.

## 2026-10-27 night: the orb is a tree whose wind-animated leaves collapse to a point
- Owner F11 run (logs/orbs_f11.log, shots 7137-7310): orbs unchanged in all modes with the
  default VS 49CD0CC3 selection -> the orb/leaves are not 49CD0CC3 draws.
- Per-draw G-buffer RT0 dump (frame_3288): the orb pixels are written by VS 9B97485ECA9FB310 /
  PS 2E65367EB25FB139 (n=2487, canopy hull, no cutout; solid on Xbox too, normally inside the leaves).
- GTAV_NATIVE_DIAG_VS=ALL x-ray dumps (frame_60472 depth_always, frame_60675 flat): healthy trees'
  leaves are VS B870B2FE / PS 1884ED2D. The orb tree's leaf draw (same world matrix as the hull,
  found by gWorld translation in draws.bin) is VS AF15EF63BB4B093F / PS 6A8524D9: **0 pixels in every
  mode**, including depth_always + flat PS -> its vertices are degenerate.
- VS AF15EF63 = wind/micro-movement tree VS: position *= _globalEntityScale (c232).wzz. In the bad
  frames c232 = (0.4, 0.4, 0, 0) for EVERY AF15EF63 draw -> all vertices collapse onto gWorld(3).
  In good frames c232 holds per-tree scales (1.0, 0.853/1.172, ...). Across all dumps the frames are
  bimodal, and both kinds occur in one run (exact.log 39680 good / 40610 bad), so it is not shadow
  vs on. Command buffers: none ran in these sessions (not that path).
- Next: find the path that writes c232 for these draws on the GPU but bypasses the D3D shadow +
  inline (83813580) capture. --gtav_native_pm4_scan=true logs ALU constant writes in the PM4
  stream between draws (SET_CONSTANT / LOAD_ALU_CONSTANT / type0 / SET_CONSTANT2, one IB level) and
  per AF15EF63 draw: captured c232 vs the last ring write to it.

## ROOT CAUSE (2026-10-27 night): XenosRecomp ignored conditional exec (cexec bN / cexec p0)
- Shadow-mode capture triggered by --gtav_native_dump_on_collapse (120 frames of collapsed leaf
  draws): native frame 7046 vs emulator 7040-7047, leaf draws n=966 / n=1092 (VS AF15EF63):
  the EMULATOR HAS THE SAME c232 = (0.4, 0.4, 0, 0) (tools/native/compare_leaf_scale.py).
- Microcode (build/ucode_disasm): every c232 use is in `cexec b0` blocks (b0 = bPlayerCollEnabled,
  player-collision displacement + entity scale); with b0 off the GPU skips them and the leaf keeps its
  unscaled position, so the game leaves c232 unset. XenosRecomp emitted CondExec / CondExecEnd /
  CondExecPred* instructions unconditionally (only CondJmp got an if), so the leaves were scaled by
  the stale zeros and collapsed; the canopy hull showed through as the orb. Bimodal per frame =
  whether the player-collision bool is on.
- Fix: shader_recompiler.cpp wraps conditional execs in `if ((g_Booleans & bN) ==/!= 0)` /
  `if ([!]p0)` and closes before the End-variant return (also fixed shouldReturn missing
  CondExecPredCleanEnd). All shaders regenerated into derive/native/census5 (affects every shader
  with conditional execs, not just trees).
- Verified unattended (logs/collapse_verify.log, dump frame_25280, "on" mode, census5 SPIR-V
  installed in build/gtav/spirv; old set in build/gtav/spirv_pre_cexec): in a frame with c232 =
  (0.4,0.4,0,0) for 120 frames, AF15EF63 leaf draws now paint pixels (48 / 121 / 0 occluded) and a
  previously bare trunk gets its full canopy. Before the fix such draws covered 0 pixels in every mode.
  Owner road test pending. Diagnostics left in (all off by default): F11 modes,
  --gtav_native_pm4_scan, --gtav_native_dump_on_collapse.
