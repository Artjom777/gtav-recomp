# Clear-sky slabs: fixed 2026-10-30 (owner: "sky looks good in this version")

Cause: GTA's deferred lights mark their volume with two depth/stencil-only passes (vs 2C40A3B0495893A9,
FA494CC72357CB0C: pass 1 dc ..8F13 stencil ALWAYS, zpass REPLACE 0x40, zfail ZERO; pass 2 dc ..8963/8A63
ref 0x20 mask 0x60, LESS, fail ZERO, zpass REPLACE). Both write all 8 stencil bits. Over the sky (depth
0, stencil 7 from the G-buffer clear) they zero the stencil, and the cloud / distant-hills layer drawn
later (vs CFA891AC2B21834E, ps DCFC61F75589ED79, stencil EQUAL 7 mask 7) got holes in the shape of each
light volume: rotating police sirens and muzzle flashes, changing every frame, only over clear sky.

Fix (backend.cpp Pipeline, --gtav_native_lightvol_keep, default on, live-switchable): depth/stencil-only
draws with write mask 0xFF whose reference and read mask only use bits 5-6 (0x60) write only bits 5-6.
The light passes test exactly those bits, so lighting is unchanged; the sky's 7 and the material ids
survive. Verified live A/B (owner + shots). Why the Xbox / Linux don't show it is not established
(the register semantics say they would zero too); possibly invisible there because the cloud and sky
colours match.

Other things added while hunting (all live cvars via build/gtav/logs/menu_set): gtav_native_colorless,
gtav_native_depth_clamp, gtav_native_farpin_scale, gtav_native_skip_ps / skip_vs, M = screenshot,
N = frame dump. Pipeline warm-up + held frames (hold_lost_frames) fix the separate pipeline-compile
flashes.

# Cutscene dialogue missing on MacBook speakers: fixed 2026-10-30 (owner hears speech again)

SDL reports 6 channels for the built-in "MacBook Air Speakers", so the SDK submitted the 5.1 mix and
macOS's own fold to the stereo speakers dropped the centre channel, where cutscene dialogue lives.
SDK `audio_force_stereo` (default on for __APPLE__, sdl_audio_driver.cpp, in
rexglue-sdk-gtav-linux.patch) always submits the SDK's stereo fold. Log line now ends "submitting 2 ch".
Install: rebuild librexruntime (cmake --build out/build/mac-arm64 --target rexruntime) and copy
out/mac-arm64/Release/librexruntime.dylib into build/gtav/.

The live-reload --input_script change (input_system.cpp) was backed out of the SDK; a copy is at
/Volumes/Untitled/gtav-captures/input_system.live.cpp. Runs with that library crashed three times
early on (guest NULL access); the link is unproven but it was removed to be safe.

# Open world reached (2026-10-30 ~10:00) — next session

Owner played past Franklin's first mission (autosave: the world loads directly). Next, in this order:
1. Open-world graphical glitches / flashes (owner: similar to the ones fixed on Linux/Windows). Launch
   with --gtav_native_flicker_catch=true and GTAV_NATIVE_SHOT_DIR shots; owner presses M at glitches,
   N (or logs/dump_request) for a frame dump; bisect with live cvars (gtav_native_skip_ps/skip_vs).
2. Performance. Open-world numbers (log aud1, ~2,400-2,650 draws): render-thread record ~33 ms/frame
   (streams 15, textures 8 incl. 3.4 hashing, bind+draw 5.5); macOS has no write watch, so guest data
   is hashed every frame.
Memory: the texture cache grew to 23,400 textures (5.8 GB footprint, 12.6 GB swap). New
gtav_native_texture_budget_mb (default 1024; LRU of textures idle 120+ frames) and a "[native] caches:"
log line every 1800 frames are built but untested; the buffer cache (`buffers`) is never pruned either.
