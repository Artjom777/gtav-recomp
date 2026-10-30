# Mac open world, 2026-10-30 (session 4): flashes mostly fixed, water drifting, next = headless perf

All uncommitted (on top of 9a8ab76 + the sky/audio work). Runs: logs/ow1..ow4.log; captures on the WD
drive /Volumes/Untitled/gtav-captures/live/{flicker*,native_dump}, older ones moved to *_old_<time>.

## 1. One-frame flashes in the open world: main cause fixed

Symptom: one-frame giant flat slabs (brown/green/grey walls), black ground, black or olive sky, streak
fans; ~5 per 1000 frames (ow2), clustered while moving through streaming areas.

Cause: the Mac renderer runs a few frames behind (record 20-33 ms per open-world frame, CPU-bound), so
the game recycled streamed meshes/textures under frames that were QUEUED but not yet recorded. The Linux
fix (rex_skip_rendering_fence + fence-time vertex copies, 10-29) only makes the GPU wait for the frame
being recorded; textures are never copied.

Fix: `gtav_native_fence_wait` (capture.cpp) now defaults to true on __APPLE__: the command processor
waits at a guest fence until the renderer recorded the draws before it, also for queued frames.
Measured live: ~5/1000 -> 2 in 21000 frames, fps unchanged (41-46), cold start + loading screens OK
(ow3). Ruled out: texture budget eviction (cache never above ~170 MB), sampled buffer hashing
(`gtav_native_exact_cache` A/B), `gtav_native_snapshot_draws`.

Leftover (rare, ~1 per 10-30k frames, bursts after long stalls):
- Correlates with fence-wait TIMEOUTS (100 ms, capture.cpp `std::chrono::milliseconds(100)` in two
  places). Any stall > 100 ms (F9 dumps take ~5 s, the flicker catcher writing notes) produced bursts
  of 35-57 timeouts and many slabs. Next: make the timeout a cvar and try 500-1000 ms.
- The glitch-only draws have SANE world matrices (c0-c3 translation near the player): real objects,
  often small LOD-looking meshes (vs 965B313196AEF6EA 216 indices at the same address 1946A310 in
  three glitches; vs 49CD0CC3ED116BB1 arena shell), location-bound (dumpster corner by "Ex Post
  Facto", near the arena). Could also be LOD fade shown solid for a frame. Owner N dumps: ow4 frames
  4335, 5199, 5337, 8577.

Tools added: `--gtav_native_flicker_min_blocks` (live; 30 skips poles passing by while driving, which
otherwise fill the 40 saves), flicker notes now print `vsc` = VS constants c0-c11 per draw. WARNING: the
catcher (with vsc) stalls the render thread and itself causes timeouts -> glitches; keep it off for
measurements, and do not press F9 casually (5 s freeze).

## 2. Water: Franklin's head under the surface, then too low, then right (oscillates)

- Not depth bias (`gtav_native_depth_bias_scale` / `gtav_native_depth_slope_scale`, new live
  diagnostics, 0/0.5/1/2: no change), not the depth buffer (colour water draws 558-583 test GREATER
  against depth 000, the same buffer as Franklin, 1x, no MSAA alias), shaders current (spirv Oct 28).
- The surface height swings over tens of seconds: too high -> right -> too low -> right.
- Wave sim (dump frame_23872 + consecutive 43904/43905): bounded (-1..+2), but the height field's MEAN
  is +0.07..+0.24 (emulator ~-0.01, same as the 10-28 Linux note "native mean +0.26"). ocean_pass1.py
  on 43904/43905: error mean -0.0003 std 0.137, fit "error ~ -0.0046 n1 - 0.0329 n2 + 0.0185" (a
  constant and noise-dependent bias). ocean_pass5.py's tiled index mapping does not match this frame
  (out == h_N + dt*v pixel for pixel, corr 0.9999) - the scripts' address regex was widened to
  0C0[89][678]000 (ping-pong now 0C086000 velocity / 0C096000 height).
- Agent finding: what the CPU sees (guest writeback of the height map, written in FrameComplete after
  the fence) is several frames old (corr 0.88-0.93 with the GPU copy). Unproven whether the game reads
  it for buoyancy.
- Next: A/B with the emulator renderer (GTAV_NATIVE=off) at the same spot; if the emulator is right,
  fix the pass-1 bias against the exact reference; optionally write back the heightfield synchronously.

## 3. Next session (owner): performance, HEADLESS

Owner wants the next session to run unattended/headless and improve fps as much as possible. The
autosave loads straight into the open world (Franklin's house), so headless runs reach a real scene.
Known numbers (ow2-ow4, open world, ~2,400-3,400 draws): 41-46 guest fps, render-thread record 20-33
ms/frame; earlier breakdown streams 15 ms, textures 8 ms (3.4 ms hashing), bind+draw 5.5 ms; no write
watch on macOS so guest buffers/textures are hashed every frame. See HANDOFF_MAC_2026-10-29.md for
earlier perf work (submit thread, rejected ideas).
