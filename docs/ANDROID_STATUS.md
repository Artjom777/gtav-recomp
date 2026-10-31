# Android port: status (2026-10-30)

**Next session (owner):** bring the Mac session's arm64 performance work (origin commits, the
HANDOFF_MAC_* / ARM64_NOTES_FOR_ANDROID.md updates) into this Android build and make it play as
well as it can. Nothing here is committed yet: merge it with the Mac work first.

Owner play-test 10-30 night: "plays pretty well". Menu: RB + Back on a controller, or the
overlay's Menu button. Max performance keeps per-pixel sun tiles on (off = gray 16x16 blocks).

GTA V runs in-game on a Galaxy S23 FE (Snapdragon 8 Gen 1, Adreno 730, Android 16), landscape and
letterboxed, with on-screen touch controls (a Bluetooth pad hides them). About 10-15 fps while the
GPU is thermally throttled (350 of 818 MHz after long sessions on the charger).

## Build and run (from Linux, phone on adb)

```sh
tools/android/build_sdk.sh            # SDK for arm64-v8a (NDK r29, API 30, armv8.2-a+lse)
tools/android/build_game.sh           # libmain.so (codegen runs with the host rexglue)
tools/android/make_spirv.sh           # game SPIR-V for Adreno -> build/android/spirv (push it)
tools/android/build_apk.sh install    # debug APK without Gradle (SDLActivity + GameActivity)
tools/android/set_args.sh [--flag=v ...] [env:NAME=V]   # files/args.txt on the phone
tools/android/run.sh [secs]           # restart, wait, print crash + log tail
```
Data on the phone: `/sdcard/Android/data/com.gtavrecomp.app/files/` with `game/disc2_files`,
`game/update`, `userdata`, `spirv`, `args.txt`, `logs/gtav.log`, `logs/stderr.txt`. Files pushed
with adb belong to the shell user: `chmod -R a+rwX` them (the app sees its own folder directly).
Phone storage is case-insensitive, like the Xbox.

Debugging without root: crashes write a tombstone (`adb logcat -b crash`) and, for faults the
SDK's handlers see, a backtrace in `logs/stderr.txt`. `tools/android/debug_crash.sh` runs the game
under lldb (lldb-server via run-as); `simpleperf record --app com.gtavrecomp.app` profiles.

## Adreno 730 / driver 0615.103 facts (Vulkan 1.1, no shaderInt64)

- 64-bit shader integers: `tools/android/lower_int64.py` lowers them to uvec2 (game shaders in
  make_spirv.sh, the renderer's own shaders via lower_header.py). The compiler also asserts on
  64-bit shifts and 2-component device-address loads; the menu shader uses a vertex buffer.
- `vkWaitForFences(UINT64_MAX)` returns VK_TIMEOUT early: plume now waits in slices. Treating
  the timeout as "done" reused in-flight command buffers and looked like random GPU faults.
- Core `vkGetBufferDeviceAddress` is a null driver entry: plume prefers the KHR one.
- minImageCount is 3: 5 swap chain images, or acquire blocks with frames in flight.
- The rect-list geometry shader crashes the compiler: rect lists on the CPU (gtav_native_rect_gs).
- Failed pipelines: plume returns null now (it returned objects with null handles).

## SDK / SDL changes (rexglue-sdk-gtav-linux.patch, sdl3-gtav.patch, plume-gtav.patch)

Android platform bits (fibers without ucontext, memfd guest arena reserved with
MAP_FIXED_NOREPLACE, ANativeWindow surface, SDL_main, GPU plugin path), `window_presenter` cvar
(one surface per window), SEH/fault handlers chain instead of dying silently, SDL's Android event
wait no longer spins (poll sentinel woke it), controller pumping rate-limited.

## Performance notes (10-30 evening)

- Pixel shaders were ~60% of the GPU frame (flat-PS A/B: 44 -> 18 ms). Shader constants now come
  from dynamic uniform buffers (set 4; tools/android/ubo_constants.py + plume
  CONSTANT_BUFFER_DYNAMIC): GPU frame at full clock ~20-29 ms (was 43-46).
- CPU recording is now the limit (~25-36 ms): vertex streams (big-endian conversion, now with a
  bulk NEON path), texture/stream hashing (no write watch on kernel 5.10), bind+draw.
- libmain is a shared library: -Bsymbolic-functions / -fno-semantic-interposition (was 10% PLT).
- Audio folded to stereo (audioserver 40% -> 9% of a core). Renderer threads pinned to big cores.
- Heat: the phone throttles the GPU to 220-350 MHz within minutes on the charger.

## Next

1. (FIXED) Blocky 16x16 tile artifacts: the resolve-to-memory compute shader (native_memory.hlsl)
   did not compile on Adreno, so no resolve reached guest memory and the game built its
   lighting-tile lists from a stale table. It compiles once lowered (lower_header.py).
2. CPU recording: write watch via mprotect, or parallel stream conversion.
3. Intermittent guest null read (sub_833CB750, once) - watch for repeats.
4. Thermal: frame cap / vsync defaults for sustained play.
