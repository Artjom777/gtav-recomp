# GTA V recomp (Linux): status, updated 2026-10-25 (early morning)

## NATIVE RENDERER: in progress (plan: ~/.claude/plans/frolicking-wandering-blanket.md)
Design (user-approved 2026-10-24): D3D-level hooks of the XDK D3D runtime + XenosRecomp shaders
precompiled offline + Plume (Vulkan first). Bring-up in "shadow" mode (native renders alongside the
emulated renderer in a second window), then "on" (no PM4, null GPU plugin).
- M0/M1 DONE: docs/d3d_map.md (D3D addresses, device register-shadow layout, baseline: 30 fps cap,
  ~0 ms swap wait, 120-220 ms spikes at scene changes = pipeline compiles).
- Code: gtav/src/native/ (d3d_map.h, stats_hooks.cpp [--gtav_native_stats], d3d_probe.cpp
  [--gtav_d3d_probe -> logs/d3d_probe.csv]); tools/native/ (genasm.py, fnsim.py, gen_probe.py);
  caches in derive/native/. thirdparty/plume (+submodules), thirdparty/XenosRecomp, ref/UnleashedRecomp.
- M2 DONE: --gtav_native=shadow opens a 2nd SDL3 window, Plume Vulkan on the RTX 4050, render thread
  (gtav/src/native/renderer.cpp), presents every guest frame (clear color only so far). Clear hook
  8380BCD8 in d3d_hooks.cpp.
- M3 shaders, first half DONE: shader create fns 83813218 (VS) / 83813030 (PS) hooked in
  gtav/src/native/shaders.cpp; --gtav_native_shader_dump=DIR dumps containers (3617 unique in a 2-min
  boot -> derive/native/shaders_rt). XenosRecomp (branch gtav, xenosrecomp-gtav.patch: textures by
  fetch slot s0-s31, 1D as 2D, dedup vertex inputs, SampleLevel in VS) + DXC: ALL 3617 compile to
  SPIR-V (tools/native/shader_census.py, 26 s). Compiles != correct; verify when drawing.
  Shared-constant ABI: tex idx (dim*32+slot)*4 for 2D/3D/Cube, samplers 384+slot*4, g_Booleans 512.
- M3-M5 first pass DONE (10-24 late night): backend.cpp replays every draw (~880/frame at 30 fps,
  ~160 pipelines), host RTs keyed by EDRAM base/format/pitch (full-size: GTA uses predicated tiling,
  draws arrive once untiled; per-tile resolves copy rect -> dest point, resolve-clears are rect
  limited), resolves -> textures keyed by guest address, textures decoded (textures.cpp, port of
  Skate 3's decoder), vertex data repacked per element (vertex_layout.h), inline BeginVertices
  draws, quad lists expanded, screen-space draws (VTE off) via g_NdcScale/Offset in XenosRecomp.
  Result: native window shows a recognizable frame (G-buffer complete, HUD text, lights);
  lighting/shadow passes still black, minimap missing.
- Tools: --gtav_native_dump_frame=N -> logs/native_dump/frame_N/ (every resolve as PPM, commands.txt
  with per-draw state); GTAV_NATIVE_SHOT_DIR (+GTAV_SHOT_SECS) native front-buffer PPMs to compare
  with GTAV_SHOT_DIR; cmake -DGTAV_NATIVE_VALIDATION=ON enables Vulkan validation on Plume only.
- Key facts: XDK Clear flags bits0-3 = RT0-3, 0x10 Z, 0x20 stencil; GTA uses reverse Z (GEQUAL,
  clear 0); SetPixelShader=83812190 / SetVertexShader=83812398 (Skate labels swapped);
  BeginVertices 83810218 / EndVertices 83810208 (vertices inline in the PM4 buffer).
- M6 progress (10-25 early): G-buffer now correct and matches the emulator; scene composition
  matches; final image over-exposed (white) — post/tonemap chain still wrong.
  Fixed: inline constants (83813580 writes ALU consts into the PM4 stream -> overlay), resolve dest
  addresses are physical (0xE0000000 range maps +0x1000; capture rewrites dest fetch base), depth
  resolves (copy shader to R32F), converting color resolves (dest host format from the guest dest
  fetch format, x32 / /32 for 16-bit fixed), rect lists (geometry shader), 16_16 fixed targets as
  SNORM with g_ColorScale, fetch exp_adjust (sN_ExpScale), per-variant resolve textures + deferred
  descriptor frees (re-pointing a bindless descriptor mid-frame corrupted earlier draws).
  Comparison tools: SDK --emu_resolve_dump_frame=F --emu_draw_dump_window=K (with
  --readback_resolve=full) -> logs/emu_dump/frame_*/ (resolves + draws.bin constants);
  tools/native/compare_dump.py (pairs native/emulated resolves by address, picks the emulated frame
  by constants), compare_constants.py, ucode_disasm (build/ucode_disasm, SDK analyzer).
  10-25 later: EDRAM-equivalent formats share targets (8888/gamma; all four 10:10:10:2 formats, with a
  7e3 re-encode when float-drawn data is resolved as fixed), resolve variants picked by fetch size,
  constant overlay invalidated by SetPending_AluConstants flushes (83818698), the 83819240 literal-list
  overlay removed (literals are embedded by XenosRecomp; leaking them broke other shaders).
  compare_values.py: numeric centre-texel comparison of every resolve (native vs emulated);
  per-draw constants now match the emulator except shader literals.
  10-25 ~01:30: packed depth+stencil resolve variant (PSDepthPack, Plume stencil views via
  plume-gtav.patch) fixed SSAO -> lighting now non-zero; native frame shows the full scene (player,
  guard, room) but washed out/hazy: HDR ~10x too bright vs emulator (luminance 0.02 vs 0.0015,
  exposure 2.56 vs 9.28). NEXT: find the over-bright stage with compare_values.py (light draws use
  additive blend, RT fmt 3 bias 0; check 7e3 decode path, exposure feedback, resolve exp bias in
  flags>>26), then minimap (black box), cube/slice resolves, perf.
  10-25 ~02:45: HAZE FIXED — native frame now matches the emulator visually (logs/native83).
  Three bugs: (1) RB_STENCILREFMASK is 0x210D and _BF 0x210C (were swapped -> G-buffer wrote
  stencil 0, the ambient/lighting PS branches on the stencil byte read through the packed D24S8
  view); (2) D3DDevice_Clear's stencil is r9 (float Z takes r8's slot); (3) vertex/index buffer
  headers hold CPU virtual addresses: capture now converts with GetPhysicalAddress (0xE0000000
  range = +0x1000) and reads the VB size from fetch dword1 bits 2-25 (was 4x too big + endian).
  Indirect vertex fetch: 44 VS fetch with a computed index (tiled lighting tile data, skinning-ish
  fetches). XenosRecomp emits vfetchIndirect(location, index, rounded) (shader_common.h); the ucode
  has format/stride 0 (D3D patches them at bind time), so the backend writes per-location records
  {raw stream addr|endian, stride dw, offset dw, D3DDECLTYPE} at shared constants 768+loc*32
  (kSharedConstantsSize now 1792) for shaders with a vs_HASH.indirect marker (shader_census.py
  writes it). Tile data is GPU-resolved into a VB: in "on" mode that VB must come from the host
  resolve (write the resolve out in guest tiled layout to a buffer) — today it reads the emulator's
  readback from guest memory.
  10-25 ~03:30: minimap/HUD fixed (838106D8 = BeginIndexedVertices(dev, prim, base vertex, vertex
  count, index count, index fmt, stride, &indices, &vertices); CPU fills ring buffers after the call,
  no End: capture copies them at the next D3D call, FlushIndexedVertices), reflection probes fixed
  (resolve dest level r8 / face r9; cube destinations = 6-layer CUBE textures, mip chains from fetch
  dword4 mip_max_level, per-(face,level) framebuffers; MSAA sources go through a single-sample
  scratch first; plume-gtav.patch sizes mip-view framebuffers), point-light shadow maps fixed
  (XenosRecomp now emits getGradients = (ddx(x), ddy(x), ddx(y), ddy(y)) coarse; 103 shaders used
  it). Light-volume draws run in RB_MODECONTROL 5 (depth-only) = no color, correct.
  The prologue room now matches the emulator pass for pass (logs/native91).
  10-25 ~04:10: "ON" MODE WORKS: ./play.sh NAME --gtav_native=on --gpu_skip_rendering=true
  (SDK cvar in the Vulkan command processor: PM4 still executes — registers, fences, interrupts,
  swaps, faked occlusion queries — but IssueDraw/IssueCopy/IssueSwap do nothing). Native window
  shows the game at 30 fps; the SDK window shows nothing. Needed: (1) GPU-written data read as
  vertices: ResolvedMemory() writes the latest resolve at an address in its guest layout (tiled,
  endian; native_memory.hlsl CSResolveToMemory) to a device buffer that vfetchIndirect records
  point at; (2) CPU readback: small resolves (<=128x128) are written back to guest memory after
  the frame fence (QueueWriteback/FrameComplete) — GTA V computes its exposure constant (PS c18)
  on the CPU from a 1x1 luminance resolve; without it exposure sat at 0.25 and lighting was dark.
  Plume patch: DEVICE_ADDRESSABLE buffers get SHADER_DEVICE_ADDRESS usage.
  Perf (prologue room, 10-25 ~04:40): emulated mode GPU 28.3 W; "on" mode GPU 18.5 W; native
  render thread per frame: command recording 6.9 ms CPU (was 27.5 ms with the Vulkan validation
  layer: build/gtav now has GTAV_NATIVE_VALIDATION=OFF — turn it ON only to debug), GPU 4.9 ms
  submit-to-fence. Both modes hold the 30 fps cap; no dropped frames after boot. The 250-330 ms
  spikes in the stats are during loading (0 draws). No shader in the prologue uses memexport.
  On mode hides the SDK window (SDL_GetWindows + SDL_HideWindow) — unverified on Wayland.
  10-25 ~13:40: FRAME CAP. The game's limiter is sub_827B5878: sleeps until N/60 s since the last
  frame (N = int at 0x8391D9D4, 2 -> 30 fps; N <= 0 skips it). gtav/src/frame_limit.cpp hooks it:
  --gtav_fps_limit=30 (default, untouched) | 0 (uncapped) | other (set N=0, pace in the hook).
  Uncapped play: --gtav_fps_limit=0 --vsync=false --gtav_native_vsync=false; on-screen counter
  --gtav_native_fps (PSDigits in native_blit.hlsl). Result: ~62 fps uncapped, 60 at limit 60.
  The ~62 ceiling: render thread waits on the update thread, which waits on events ~60% of the
  time; the SDK TimerQueue thread is busy -> suspect a 60 Hz guest timer (KeSetTimer) pacing
  the update loop. Not investigated further yet. Game-speed side effects at >30 fps unchecked.
  NEXT: other scenes (outdoors, water, particles, cutscenes) as the user plays; M7/M8: "on" mode
  (null GPU plugin, Plume owns the swapchain, no PM4), queries/fences, PSO precompile, perf; and
  in "on" mode GPU-written vertex data (tiled lighting tile VB) must come from host resolves.
  (old) BLOCKER: lit HDR scene (resolves to 06AA0000) is 0 because SSAO (06791000) is 0: the SSAO
  shader samples the depth/stencil resolve (1F1F8000, k_24_8) through an 8888 fetch to read the raw
  D24S8 bits -> need a packed depth24+stencil8 variant of depth resolves (stencil aspect). Also
  resolve flags exponent bias (flags >> 26, signed 6-bit; e.g. EC000000 = -5) not applied yet.
  Open: constants alignment of rect-list draws (pair by shader too), cube/slice resolves (dest
  face/level args r8/r9 ignored, cube textures bound as null), MSAA depth resolves, point sprites.
- NEXT (M6): make passes correct one by one with the dump tool: shadow maps (R16G16 256/128 targets
  black), lighting/HDR chain (037+ black), minimap, depth resolves (skipped), rect lists (half drawn),
  point sprites, then performance (pipeline precompile, device-local buffers).

## Current state (2026-10-24 evening): plays well past the prologue vault
User report from run play5: "ran great, no issues except small pauses on the cutscenes and such".
Played ~11 min past the vault with no crash. Fixes in effect:
- Cutscene audio workaround (gtav/src/cutscene_audio_fix.cpp), fires once at the vault.
- Removed bogus manifest entry 0x82E548CC (crash ~2 min after the vault).
- Added vtable thunks 0x834A2E10/20/30 (crash ~2.5 min after the vault).
Known: small pauses at cutscenes (likely shader/pipeline compiles and the slow USB HDD);
one run (play4) froze at boot before audio init, not reproduced (machine was heavily loaded).

## 2026-10-24 (late, 2): crash at 0x834A2E30, FIXED
`Call to invalid or unregistered function at 0x834A2E30` (logs/play3.log): vtable thunks
0x834A2E10/20/30 (16 bytes each, after sub_834A2E00) were missing. Added with size 0x10. A whole-image
scan for unregistered `lwz r12,0(r3); lwz r11,X(r12); mtctr; bctr` thunks after a bctr found only these 3.

## 2026-10-24 (late): crash ~2 min after the vault, FIXED
`[FATAL] Unresolved branch from 0x82E54998 to 0x82E5481C` (logs/play2.log). The manifest entry
0x82E548CC "pointer target" was a false positive (it equals a native hash in natives.json) and
split sub_82E54790 mid-loop. Removed it; now 0 unresolved branches in generated code. Note:
gtav_manifest.toml has CRLF line endings. 0x829ED654 is also a hash collision but harmless.

## 2026-10-24: prologue vault hang WORKED AROUND (user got past the vault cutscenes)
Root-cause chain (traced with gtav/src/cutscene_trace.cpp, logs trace2/trace3/fix1):
- At cutscene end the game releases the cutscene audio (obj 0x83A320F8, ctrl 0x83A320C0) in
  "play-out" mode (audio+5072 = 1; sub_823F30F0 called with r4 = 0): it waits for each stream to
  stop playing by itself before freeing the stream slot (ctrl+10296, 2 slots) and Resetting.
- In our runtime a stream plays to its end (stream +124 position ms >= +136 end ms; +128 start ms)
  and then stays "playing" (+112 & 3 == 2) forever -> slot never freed -> audio stuck in state 5
  (+5068) -> state-2 attach handler sub_824DC130 waits on IsBusy -> HAS_CUTSCENE_LOADED never true
  -> black screen + spinner. Leaked streams also eat the 2 slots (vault-exit cutscene hang).
- Workaround (gtav/src/cutscene_audio_fix.cpp; old tracing moved to tools/old_traces/): from the sub_824DC130 hook, when
  audio state is 5 and its streams are at their end, call __imp__sub_823F30F0(audio, 1, 10 ms)
  -> slots freed + Reset. After that first forced Reset, the game's own releases go immediate.

### Next
1. DONE 10-24: workaround split into cutscene_audio_fix.cpp, tracing dropped. Keep playing (log logs/play2.log):
   and keep playing: confirm "Introduction" completes; watch for other streamed audio that never ends.
2. Real fix: find why a stream at its end never reports finished (what moves stream +112 out of
   state 2): likely an XMA context/voice completion the runtime never delivers. Compare against
   Xenia (plays it fine). Already tried: xenia-canary XMA Work() last-frame drain (kept, didn't fix).
   Low suspect: NtReadFile returns PENDING for async reads (upstream returns the real status).
3. Notes: /dev/shm leak (play.sh now cleans it); kernel-header update -> delete stale SDK PCHs;
   the TOSHIBA HDD is slow if other jobs scan it.

---
(older notes below)


## Where things stand
- Builds and runs natively on Linux: `~/gtav-linux/play.sh [logname] [runtime flags]`.
  Env: `GTAV_VULKAN_DEVICE` (default 1 = RTX 4050), `GTAV_LOG_LEVEL` (info), `GTAV_SHOT_DIR` /
  `GTAV_SHOT_SECS` (periodic PPM frame captures, gtav_app.h).
- SDK: `~/gtav-linux/rexglue-sdk` (v0.10.0 + Windows agent patches + Linux fixes), Release build,
  install in `out/install/linux-amd64`. After rebuilding the SDK, copy `out/install/linux-amd64/lib/*.so`
  into `~/gtav-linux/build/gtav/`. Full SDK diff: `~/gtav-linux/rexglue-sdk-gtav-linux.patch`.
- Game: `cmake --build ~/gtav-linux/build/gtav -j6` (codegen reruns only if the manifest changes, ~8–15 min).

## Fixed this session
1. **Red tint / green tint on NVIDIA**: `render_target_cache.cpp` GetTransferShader — the
   depth->stencil-bit fallback (used when `VK_EXT_shader_stencil_export` is missing, i.e. NVIDIA)
   never emitted the discard (`packed` stayed NoResult because the depth texture isn't loaded in
   stencil-bit mode), so every transfer wrote stencil 0xFF. Backported upstream xenia-canary fix
   (`if (packed == NoResult && mode.output == kStencilBit) packed = source_stencil[0];`).
   Found by bisection: skipping mode 5 (kDepthToStencilBit) removed the tint.
2. NVIDIA selection (`--vulkan_device=1`), prctl(PR_SET_PTRACER_ANY) so `gdb -p` works without root.

## Open issue 1 — prologue "money grab" hang (TOP PRIORITY)
Symptom: after grabbing the vault money the screen fades to black with a spinner forever.
Facts established:
- Same hang on the base (v9) build and on TU27, so the TU does NOT fix it, and Xenia plays TU27
  through the prologue -> it is a bug in our recomp/runtime.
- Function map base->TU27 (tools/fn_match.py): base handshake sub_822DE960 == TU27 sub_83583338
  (per-frame update/render sync, completes fine on TU27); main loop sub_82235E38 alive.
- At the hang: main loop runs, loading screen renders, XMA decoder idle, no streaming. Only file
  activity is the music stream (handle on audio_music.rpf, ~1.8 MB per 16 s) — audio is alive.
  World rendering stops at the fade (last viz query). No errors at debug level.
- All observed NtReadFile calls are synchronous (no event/APC), so async-completion loss is
  unlikely (but note: ReXGlue returns STATUS_PENDING for every async file read, even failures;
  Xenia returns the real result — worth aligning).
- Conclusion: the mission script (prologue1) is waiting on a native that never reports ready.

Evidence saved: `logs/hang3.log` (trace + log_noisy, kernel import trace), `logs/hang3_1/2.txt`
(gdb stacks), `logs/hang3_guestmem/guest.bin` + `index.txt` (4 GB guest memory snapshot at the
hang; index lines = guest_start guest_end file_offset, guest addr = host - 0x100000000).

Plan:
1. Find the script thread for prologue1 in the snapshot: search for joaat("prologue1")
   (big-endian) and nearby scrThread/scrThreadContext (ThreadId, ScriptHash, State, PC, FP, SP).
   Get its PC.
2. Extract prologue1.xsc (check update.rpf TU27 patch first, then disc script.rpf; RPF7 on 360,
   TOC AES-encrypted) and decompile with EROOTIIK/GTA-V-Script-Decompiler (`vconsole` opcodes) or
   njames93/GTA-V-Script-Decompiler. Look at the code at PC: which native is it polling?
3. Find that native's handler in the recomp (native registration table: hash -> fn ptr) and
   instrument it (log return / inputs) to see why it never becomes true; trace back to the
   subsystem (cutscene manager, streaming, audio prepare...).
4. Cheap experiments while doing that (each needs a playthrough to the vault):
   `--ignore_thread_priorities=true`, `--ignore_thread_affinities=true`, `--clock_no_scaling=true`,
   `--protect_zero=false`; add pending manifest entry 0x834A2E10.
5. Workaround if needed: a post-prologue save ("Introduction completed" save) in userdata.

## Open issue 2 — phone has a black box (host render-target path only)
Fine with `--render_target_path_vulkan=fsi` (laggy). Likely alpha lost in a host-RT format path
or resolve. Next: test `--gamma_render_target_as_unorm16=false`, check k_8_8_8_8_GAMMA /
2_10_10_10 transfers and resolves for the phone RT; compare against upstream xenia-canary.

## Later
- Port xenia-canary's newer XMA decoder if audio issues show up (ReXGlue has the old one).
- Native renderer (like the Skate 3 recomp) once the game is stable.
