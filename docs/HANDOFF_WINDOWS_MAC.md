# GTA V recomp: native renderer handoff for Windows (and later macOS)

Written 2026-10-25 on Linux, at the end of the native-renderer sessions. Pick this up on
Windows first; macOS notes are at the end. `NEXT_SESSION_PLAN.md` (top) is the detailed
chronological status; `docs/d3d_map.md` holds the D3D addresses and device layout. The
design plan is in "Design" below.

**Rule that still applies:** never use the leaked Rockstar source ("GTAVCleanRoomReference"),
not even for names.

## Where things are

| What | Where |
|---|---|
| Snapshot of the Linux repo (sources, patches, tools, docs) | `E:\gtav-rex\linux-native-2026-10-25\repo\` |
| Shader containers dumped from the game (3617, game-derived, private) | `...\linux-native-2026-10-25\derive\native\shaders_rt\` |
| Precompiled SPIR-V + `vs_*.indirect` markers (3661 files) | `...\linux-native-2026-10-25\derive\native\census1\spirv\` |
| Git repo (private) | `https://github.com/andrewnakas/gtav-recomp` — **the native renderer is NOT committed yet**; the snapshot above is the only copy off the Linux disk |
| Old Windows tree / build | `E:\gtav-rex\` (SDK, `gtav\`, game data), build output `D:\gtav-build` (see `HANDOFF_LINUX.md` there) |

Third-party pins (clone these, then apply our patch):

| Repo | Commit | Our patch |
|---|---|---|
| `rexglue/rexglue-sdk` | `f5337cd` (v0.10.0) | `rexglue-sdk-gtav-linux.patch` (all SDK changes, incl. the Windows-era ones) |
| `renderbag/plume` (recursive submodules) | `0ce184b` | `plume-gtav.patch` |
| `hedge-dev/XenosRecomp` (recursive submodules; brings `dxc-bin` with Windows `dxc.exe`) | `990d03b` | `xenosrecomp-gtav.patch` |
| `hedge-dev/UnleashedRecomp` (reference only, `ref/`) | `cf829a9` | — |

Layout the CMake expects: `<repo>/thirdparty/plume`, `<repo>/thirdparty/XenosRecomp`,
`<repo>/rexglue-sdk`, `<repo>/derive/native/census1/spirv`.

## What works (Linux, prologue room, 2026-10-25)

- **Native renderer** (`gtav/src/native/`): hooks the statically linked XDK D3D layer,
  replays each guest frame through Plume (Vulkan) with host render targets (no EDRAM
  emulation), XenosRecomp shaders precompiled to SPIR-V. Matches the emulated renderer pass
  for pass in the prologue room: G-buffer, deferred + tiled lighting, SSAO, reflection probes
  (cube + mips), point-light shadows, post/tonemap, HUD + minimap.
- **"on" mode**: the emulated GPU still executes PM4 (fences, interrupts, swaps, faked
  occlusion queries) but draws nothing (`--gpu_skip_rendering`, SDK patch); the native
  renderer owns the frame. 30 fps cap: GPU 18.5 W vs 28.3 W emulated; native render thread
  ~7 ms CPU record + ~5 ms GPU per frame.
- **Frame cap removed**: `--gtav_fps_limit` (0 = uncapped ≈ 62 fps, 60, default 30 = the
  game's own limiter) + on-screen counter `--gtav_native_fps`.

Launch flags (Linux `play.sh` passes the data roots; reuse them in a Windows script):

```
# play, native only, 60 fps, counter
--gtav_native=on --gpu_skip_rendering=true --vsync=false --gtav_native_vsync=false --gtav_fps_limit=60 --gtav_native_fps
# side by side with the emulated renderer (debugging): --gtav_native=shadow
# frame dump for comparisons: --gtav_native_dump_frame=1500 (+ SDK --emu_resolve_dump_frame=1499
#   --emu_draw_dump_window=4 --readback_resolve=full in shadow mode) -> logs/native_dump/, logs/emu_dump/
# stats: --gtav_native_stats
```

## Windows: steps

1. **Code.** Copy `repo\` from the snapshot (or, if the native work gets committed and pushed
   first, clone). Clone the three third-party repos at the pinned commits into the layout
   above (`git submodule update --init --recursive` inside plume and XenosRecomp), and apply
   the patches with `git apply`.
2. **SDK.** The Windows build used the older `rexglue-sdk-gtav.patch`; the Linux patch is a
   superset (native-renderer dump hooks, `gpu_skip_rendering`, Linux fixes). Apply it to a clean
   `f5337cd`, rebuild + install the SDK as before. Check the Linux-only hunks still compile on
   MSVC/clang-cl (xam/content, xobject, xsemaphore changes were Linux bring-up fixes).
3. **CMake (`gtav/CMakeLists.txt`) — Linux-specific bits to port:**
   - The native shader step runs `dxc-linux` with `LD_LIBRARY_PATH`; on Windows use
     `${GTAV_DXC_DIR}/bin/x64/dxc.exe` (no env). Easiest: pick the binary by `if(WIN32)`.
   - Plume is added with `PLUME_SDL3_VULKAN_ENABLED` — fine on Windows. Keep Vulkan first.
   - `GTAV_NATIVE_VALIDATION` (Vulkan validation layer) must stay **OFF** for play: it made
     recording 4x slower.
   - `frame_limit.cpp` and `src/native/*.cpp` are already in the source list.
4. **Hardcoded paths to replace** (all `getenv("HOME") + "/gtav-linux/..."`):
   - `gtav/src/native/backend.cpp` (`Shader()`: SPIR-V dir; `WriteDump()`: dump dir)
   - `gtav/src/native/renderer.cpp` (`SpirvDir()`)
   - `gtav/src/native/d3d_probe.cpp` (probe CSV)
   Pass `--gtav_native_spirv_dir=E:\...\census1\spirv` meanwhile; better default: a `spirv\`
   folder next to the exe, and logs next to `--log_file`.
5. **Shaders.** The SPIR-V in the snapshot is platform-independent — copy it and go. To
   regenerate (new shaders seen in play, XenosRecomp changes): build XenosRecomp on Windows,
   then port `tools/native/shader_census.py` (`XR`, `DXC` paths, no `LD_LIBRARY_PATH`). It also
   writes the `vs_HASH.indirect` markers the backend needs. New shader containers are dumped
   with `--gtav_native_shader_dump=DIR`.
6. **Run** shadow mode first (`--gtav_native=shadow`) and compare the two windows, then "on".
   `--vulkan_device` picks the GPU for the SDK; `--gtav_native_device` (substring, default
   `NVIDIA`) for the native renderer.
7. **Tools** that are Linux-only: `play.sh` (write a `.cmd`/`.ps1` with the same flags),
   `tools/native/ucode_disasm.cpp` (links SDK objects; rebuild against the Windows SDK build),
   guest-memory reads via `/proc/<pid>/mem` (Windows: the old `tools/stackdump/peek.ps1`).

### D3D12 later (optional)

Plume has a D3D12 backend, but our shader ABI is Vulkan-only today: XenosRecomp output uses
`vk::RawBufferLoad` (buffer device addresses) for constants, shared constants and
`vfetchIndirect` under `#ifdef __spirv__`. A D3D12 path needs the non-SPIR-V branch of
`shader_common.h` extended (cbuffer / root descriptors for the shared constants incl. the
indirect-fetch records at 768..1791, `ByteAddressBuffer` for raw vertex reads), DXIL compiles
(dxc without `-spirv`), and the backend's pipeline layout / push constants mapped to root
signatures. UnleashedRecomp's `video.cpp` is the model. Vulkan on Windows is the fast route.

## macOS (after Windows)

- First check the **SDK**: ReXGlue presets are win/linux amd64; the runtime (4.8 GB guest
  arena via shared memory, host = guest + 0x100000000 mapping, threading, x86 intrinsics in
  generated code) needs an arm64/macOS port before the renderer matters.
- **Renderer, quickest:** MoltenVK with the existing Vulkan path. Needs
  `bufferDeviceAddress` (MoltenVK supports it on Metal 3 devices), geometry shaders (used for
  Xenos rect lists, `native_rectlist.hlsl` — **Metal has none**; expand rect lists on the CPU
  or in the vertex shader instead), and `ddx_coarse`/`ddy_coarse` (fine).
- **Renderer, proper:** Plume's Metal backend + shaders via DXIL → Apple Metal Shader Converter
  (what UnleashedRecomp does) or SPIR-V → MSL (SPIRV-Cross); same ABI caveats as D3D12.

## Design in one page

- **Capture** (`d3d_hooks.cpp`, `capture.cpp`): `REX_HOOK_RAW` overrides of the D3D functions
  (draws, BeginVertices / BeginIndexedVertices, Set*Shader, SetStreamSource, SetIndices, Clear,
  Resolve, Swap, inline-constant writes, SetPending flushes). Each draw snapshots the register
  shadow, fetch constants, VS/PS constants, vertex declaration and streams into a
  `FramePacket` arena (`frame.h`); Swap hands the packet to the render thread.
- **Backend** (`backend.cpp`, ~2000 lines): bindless texture heap, pipelines keyed by state,
  render targets keyed by EDRAM base/format/pitch (full size: GTA uses predicated tiling),
  resolves into textures keyed by guest address (variants by size/format, cube faces, mips),
  texture decode cache (`textures.cpp`), vertex repacking (`vertex_layout.h`), rect lists via a
  geometry shader, resolve-to-guest-layout compute shader (`native_memory.hlsl`) for GPU-written
  vertex data and for CPU readback in "on" mode.
- **Shaders**: XenosRecomp (patched) → HLSL → DXC → SPIR-V, loaded by hash at runtime.
  Shared-constant ABI and push constants are documented in `shader_common.h` and the backend.

## Hard-won facts (don't rediscover these)

- Clear: stencil arg is **r9** (float Z takes r8's slot). Flags: bits 0-3 RTs, 0x10 Z, 0x20 stencil.
- `RB_STENCILREFMASK` = 0x210D, `_BF` = 0x210C.
- Resource headers (VB/IB/resolve dest) hold **CPU virtual** addresses: convert with
  `GetPhysicalAddress` (0xE0000000 range = physical + 0x1000). VB fetch dword1: endian bits
  0-1, size in dwords bits 2-25.
- 838106D8 = BeginIndexedVertices (HUD/minimap); no End call — copy data at the next D3D call.
- 44 vertex shaders fetch vertices by computed index; the ucode has format/stride 0 (D3D
  patches at bind time) → `vfetchIndirect` + per-location records from the declaration.
- getGradients must be emitted (103 shaders; shadow maps were all zero without it).
- Light-volume draws run in RB_MODECONTROL 5 = depth only (no color, correct).
- Exposure (PS c18) is computed on the CPU from a 1x1 luminance resolve → "on" mode must write
  small resolves back to guest memory (`QueueWriteback`), or the image goes ~9x dark.
- Frame limiter: `sub_827B5878`, N at 0x8391D9D4 (×1/60 s); N ≤ 0 skips it.

## Open items

1. Only the prologue room is verified pass by pass. Untested: outdoors/city, water, particles,
   cutscenes (the original complaint was cutscene pauses — check them in "on" mode), driving.
2. The user saw "glitches" in test windows on 10-25 — not yet characterized; get screenshots.
3. ~62 fps ceiling when uncapped: update thread waits on events ~60% of the time, SDK timer
   thread busy — suspect a 60 Hz guest timer. Also check game speed/physics above 30 fps.
4. "on" mode hides the SDK window (`SDL_HideWindow`) — unverified (Wayland); check on Windows.
5. Pipeline precompile at startup (none yet; driver cache helps; no hitches measured in-room).
6. D3D12 / Metal backends (see above).
