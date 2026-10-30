# Next session: Android port, built on the Linux laptop (start here)

Written 2026-10-30 on Linux after merging the macOS arm64 port into `main` (8dda291). The owner
wants the next session to fully port the recomp to Android, building on this Linux machine with the
phone plugged in over USB.

Read in this order: this file; `docs/ARM64_NOTES_FOR_ANDROID.md` (Mac findings: tile-GPU pass cost,
memory, write watch, the dxc/sun-tile variant trap); `docs/MAC_THEN_ANDROID_PLAN.md` section 4;
`docs/HANDOFF_MAC_2026-10-30_openworld.md`. Rules unchanged: never use the leaked Rockstar source;
game files and anything derived from them never go into git.

## State at handoff

- `main` = Linux clean-room release + Windows release + macOS arm64 port (MoltenVK, ~45-55 fps open
  world on an 8 GB M1). The merge was not built on Linux yet: Mac CMake changes are guarded by
  `APPLE`, but do one Linux build first to confirm.
- Local `rexglue-sdk/` checkout does NOT have the Mac delta: `git -C rexglue-sdk diff` differs from
  `rexglue-sdk-gtav-linux.patch`. Reset the SDK to its pin and re-apply the patch, then rebuild and
  install the SDK (see the Setup notes in `docs/HANDOFF_WINDOWS_MAC.md`). Apply `plume-gtav.patch` again too.
- adb: `~/android-tools/platform-tools/adb` (downloaded 10-30; not on PATH).
- Game data: the ISOs and `gtav_mac_bundle_2026-10-29/` are on the phone in `/sdcard/Download/`;
  `scripts/xiso_adb.py` extracts the ISOs on the phone over adb.

## Blockers to clear first (10-30)

1. **The phone was not detected**: `lsusb` showed no Android device and `adb devices` was empty.
   Use a data cable, unlock the phone, pick "File transfer", enable Developer options and USB
   debugging, then accept the RSA prompt. On Ubuntu `adb` may also need a udev rule for the vendor
   (Samsung 04e8) or `plugdev` membership.
2. **Disk**: the SSD (`/`) has 5.8 GB free and the TOSHIBA drive has 9.6 GB. The NDK (~2.5 GB),
   the SDK cmdline tools and platform (~1 GB), Gradle and a JDK (~1 GB) and the build tree need
   ~10+ GB. Free space on `/` first (old /dev/shm files, `build/gtav/logs/native_dump`, flicker
   captures, `~/.cache`). Do not put the NDK on exFAT (no exec bits/symlinks).
3. No JDK installed (`/usr/lib/jvm` absent). Either install openjdk-17 (needs sudo, owner) or skip
   Gradle: a code-free NativeActivity APK (`android:hasCode=false`) packs with `aapt2` and signs
   with `apksigner`, and both of those need Java too. A portable JDK tarball in `~/android-tools`
   avoids sudo.

## Proposed milestones (confirm with the owner before starting)

- A0 Toolchain: cmdline-tools, NDK r27+, platform 34/35, build-tools; `adb shell getprop` for the
  device (SoC/GPU, RAM, kernel, page size: `getconf PAGESIZE`, Vulkan version via `vulkaninfo`
  or `dumpsys`).
- A1 SDK + game cross-compile for arm64-v8a with the NDK's clang (`-DANDROID_ABI=arm64-v8a`,
  API 29+; reuse the Mac `gtav_register.cpp` -O0/no-GlobalISel rule). SDL3 Android backend for the
  window/input/audio (AAudio). Guest memory: memfd instead of the /dev/shm file.
- A2 APK shell (SDL's `SDLActivity` or a NativeActivity), data on app-specific external storage,
  case-insensitive path lookup (Android storage is case-sensitive; the SDK has a case scan,
  disabled only on macOS volumes).
- A3 Boot to the title on the emulated renderer, then `GTAV_NATIVE=on`: SPIR-V from the bundle,
  pre-made `ps_D258EBE32001470C_tiles.spv` variant (no dxc on the phone; without it the world is
  flat grey, see ARM64 notes section 7), controller via SDL gamepad.
- A4 Mobile perf/memory: fewer render passes (tile GPUs), bounded pipeline warm-up, skip-rendering
  shared memory small, hashing fallback (no userfaultfd on Android).
