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
