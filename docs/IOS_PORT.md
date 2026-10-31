# iOS (iPhone) port — dev build

Started 2026-10-31 on the macOS arm64 work (main c3d51dd); rebased onto the Android branch (branch ios-android). Target: the owner's iPhone 13 mini
(A15, 4 GB RAM, iOS 26.4.2), Xcode 27.1 / iOS SDK 27.1. **Dev build only: the app contains the
recompiled game code and must never be published** (see RELEASE_CLEANROOM.md for the rule and
"Release plan" below for how players could get it).

## Build

```sh
# 1. SDK runtime for iOS (librexruntime.dylib, librexgpu-xenos.dylib; no codegen tool, no loader)
cd rexglue-sdk
cmake -S . -B out/build/ios-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_STANDARD=23 \
  -DREXGLUE_ENABLE_TRACY=OFF -DCMAKE_INSTALL_PREFIX=$PWD/out/install/ios-arm64
ninja -C out/build/ios-arm64 && cmake --install out/build/ios-arm64
# the install registers the package in ~/.cmake/packages/rexglue: delete that entry, or a Mac
# configure could pick up the iOS SDK.

# 2. Game (monolithic, generated code from gtav/generated as on the Mac; ~5 min -j4)
cd ../gtav
cmake -S . -B ../build/ios -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_FLAGS=-mcpu=apple-a15 \
  -DCMAKE_PREFIX_PATH=<repo>/rexglue-sdk/out/install/ios-arm64 \
  -DCMAKE_FIND_ROOT_PATH=<repo>/rexglue-sdk/out/install/ios-arm64 \
  -DGTAV_PREBUILT_NATIVE_SHADERS=<repo>/build/gtav/native_shaders
ninja -C ../build/ios -j4

# 3. Game shaders for iOS (constants in uniform buffers, as on Android) from a shader cache
#    made by a Mac install (the clean-room test install has all 3,604):
TOOLS=/Volumes/Untitled/gtav-cleanroom-test/tools CACHE=/Volumes/Untitled/gtav-cleanroom-test/data/shader_cache \
  tools/ios/make_spirv.sh 4          # -> build/ios/spirv

# 4. App bundle, signing, install, args (+ shaders) (needs a profile for com.nakas.gtav, below)
tools/ios/deploy.sh [--spirv]        # = make_app.sh --install + copies to Documents
tools/ios/launch_when_unlocked.sh build/ios/run.log   # console log; game log: Documents/logs/gtav.log
```

Patches: rexglue-sdk-gtav-linux.patch, plume-gtav.patch, sdl3-gtav.patch (rexglue-sdk/thirdparty/sdl3)
and ffmpeg-gtav.patch (rexglue-sdk/thirdparty/FFmpeg), applied as on the other platforms. Branch
ios-android = origin/android + the iOS commits; phone paths shared with Android are `GTAV_MOBILE`
in backend.cpp (UBO constants, load-op clears, texture recheck interval) and the letterbox in
renderer.cpp.

Provisioning profile: minted once with a throwaway Xcode project (xcodegen) carrying bundle id
`com.nakas.gtav`, team FBCD7748P3 and the two kernel entitlements, built with
`xcodebuild -target <t> -sdk iphoneos -allowProvisioningUpdates -allowProvisioningDeviceRegistration`.
make_app.sh signs with the profile's own entitlements (application-identifier etc. included).

MoltenVK: the prebuilt ios-arm64 dynamic framework from the Skate 3 iOS port (1.4.2,
`MOLTENVK_FW`), copied into the bundle root as libMoltenVK.dylib. The SDK and plume open it by
its full path next to the executable (iOS has no Vulkan loader; a bare dlopen name does not
search the bundle).

## Bundle layout

`GTAV.app/`: gtav (exe, @executable_path rpath), librexruntime.dylib, librexgpu-xenos.dylib,
libMoltenVK.dylib (each signed), Info.plist (landscape, fullscreen, UIFileSharingEnabled,
UILaunchScreen, game controllers), embedded.mobileprovision.

## Data on the phone (app Documents, visible in Finder / Files)

```
Documents/args.txt          launch arguments (tools/ios/args.txt), relative to Documents
Documents/game/disc2_files  game data root        (7.7 GB)
Documents/game/update       TU27 update root      (54 MB)
Documents/userdata          user data incl. the installed Disc 1 content (7.6 GB) and saves
Documents/spirv             game shaders, translated on the Mac (build/gtav/spirv, 75 MB)
Documents/logs, shader_cache, gtav_settings.ini   written by the game
```

Copy with `xcrun devicectl device copy to --device <udid> --domain-type appDataContainer
--domain-identifier com.nakas.gtav --source <dir> --destination Documents/<dir>` (~19 MB/s USB;
works while the phone is locked; launching does not). The phone needs ~16 GB free.

## What changed for iOS

SDK (rexglue-sdk, uncommitted on top of the SDK patch):
- `REX_PLATFORM_IOS` (TARGET_OS_IPHONE; REX_PLATFORM_MAC stays set), `REX_PLATFORM` ios-arm64
  (own out/ dir), no codegen tool / Vulkan loader / MoltenVK source build on iOS.
- memory_posix: guest memory backed by an unlinked sparse file in TMPDIR (the sandbox denies
  shm_open), no MAP_JIT, vm_region_64 (mach_vm.h is unsupported in the iOS SDK).
- windowed_app_main_sdl: main() -> SDL_RunApp (UIKit owns the main thread); arguments from
  Documents/args.txt; cwd = Documents. SDL video driver "uikit".
- vulkan_instance: libMoltenVK.dylib next to the executable first.
- Touch gamepad: the Android port's gtav/src/native/touch_controls.cpp (on by default on iOS).

Game / plume:
- plume_apple.mm: UIKit branch (UIWindow size x nativeScale, vendor APPLE, no IOKit).
- plume_vulkan.cpp: volkInitializeCustom with the bundled MoltenVK.
- renderer.cpp: reuses the SDK's (only) window, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER.
- paths.h: ExeDir() = $HOME/Documents (the bundle is read-only).
- shader_compiler.cpp: no std::system on iOS, gtav_native_compile defaults off: every shader
  must come precompiled from the Mac (Documents/spirv).
- safe_read.h: vm_read_overwrite.
- gtav/CMakeLists.txt: GTAV_IOS, imported host rex::rexglue for the codegen rule, no tools/ staging.

## Release plan (draft, 2026-10-31)

Built and checked on the Mac (2026-10-31, not yet run on the phone):
- Shell: `cmake ... -DGTAV_GAME_DLL=ON -B build/ios-shell` -> gtav_fast (3 MB, no game code; the
  stub gtav_game.dylib is built for iOS via GTAV_STUB_SDK=iphoneos in gen_interface.py).
- Game library: `build_game_dll.py <llvm-bin> <copy of generated> build/ios-shell/game_interface
  <out> --ios --runtime <ios librexruntime.dylib>` with the release's bundled LLVM 19.1.7
  (clang++ / ld64.lld links to clang-19 / lld): 406 TUs in 286 s -j4, 115 MB, platform iOS, links
  against tools/game_dll/libSystem_ios.tbd (no Xcode SDK needed).
- Package: `GTAV_GAME_DYLIB=<out>/gtav_game.dylib tools/ios/make_app.sh`.

Constraint: like the other releases, nothing Rockstar-derived may be published, and iOS can
neither compile code nor load unsigned code. So the player's **Mac** does the building, as the
macOS clean-room GTAV.app already does for the Mac:

1. Publish an **iOS shell** (no game code): gtav exe built with `GTAV_GAME_DLL=ON`, the SDK
   runtime, the GPU plugin, MoltenVK, Info.plist. Same split as the Mac release (game_bridge.cpp,
   gen_interface.py exports list). Taint scan as for the other packages.
2. The macOS GTAV.app setup window gets an "iPhone" step: from the player's discs + TU27 (already
   copied and hash-checked for the Mac install) it cross-compiles the same generated code for
   `arm64-apple-ios17` with the bundled LLVM 19.1.7 (`-mcpu=apple-a14`), links
   `gtav_game.dylib` with ld64.lld against .tbd stubs (as tools/game_dll/libSystem.tbd does for
   macOS; no Xcode needed to build), translates every shader on the Mac (it has dxc) into
   spirv/, and drops the dylib into the shell.
3. Signing + install with the player's own Apple ID. Options: (a) Xcode installed: the tool runs
   `devicectl` and signs with the player's development certificate; (b) no Xcode: the tool
   writes an unsigned .ipa for Sideloadly / AltStore, which re-sign nested dylibs.
4. Game data: the tool pushes disc2_files, update, the installed Disc 1 content and spirv to the
   app's Documents over USB (devicectl), or the player drags them in Finder (file sharing).

Open questions to measure on the dev build first:
- Free Apple IDs cannot get `increased-memory-limit` / `extended-virtual-addressing`. A14+ gets a
  large address space without the second (Skate 3 finding); the first matters if the footprint is
  above the ~2 GB default jetsam limit on 4 GB phones. Measure the footprint here.
- 7-day re-signing for free accounts (AltStore refreshes it).
- Minimum device: A12+ (Metal/MoltenVK features, 3-4 GB RAM) — to be measured.
