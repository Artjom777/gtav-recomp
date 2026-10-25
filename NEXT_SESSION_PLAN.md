# GTA V recomp (Linux): status, updated 2026-10-24 (evening)

## NEXT SESSION: planning session, then build a NATIVE RENDERER
User's goal: plan and fully build a native renderer so the game runs great (model: the Skate 3
recomp's native renderer). Start with a planning session (scope, architecture, milestones)
before writing code. Current renderer = ReXGlue's xenia-derived Xenos/Vulkan emulation
(--gpu_plugin=xenos), which works but stutters.

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
