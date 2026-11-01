#!/bin/bash
# The Android part of every desktop release (the package's android/ folder): the public APK with
# no game code, plus what the Android step (loader/src/install.cpp RunAndroid, on Windows, Linux or
# macOS) needs to build the player's libgtav_game.so for it with the bundled clang + lld:
#   OUT_DIR/GTAV.apk             the app: SDL + setup screen, libmain.so (shim), libgtav_fast.so,
#                                the SDK runtime; no game code, data or shaders
#   OUT_DIR/link/                libgtav_fast.so, librexruntime.so: byte for byte the APK's own
#                                (the game library links against them; pack.txt names the hash)
#   OUT_DIR/interface/           gtav_hooks.h, gtav_game.exports (names only)
#   OUT_DIR/include/simde/       simde (MIT): SSE intrinsics on NEON for the generated code
#   OUT_DIR/LICENSES/            simde, SDL, libc++ (what the APK adds to the desktop package)
#   tools/android/make_shell.sh OUT_DIR
# Inputs: build/android-dll (tools/android/build_game.sh layout with -DGTAV_GAME_DLL=ON, see
# docs/ANDROID_STATUS.md) and the Android SDK install (tools/android/build_sdk.sh).
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$1
DLL=$R/build/android-dll
source ~/android-tools/env.sh
NM=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm
READELF=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

# The shell must be the release layout: no recompiled game function defined anywhere in it.
"$READELF" -d "$DLL/libgtav_fast.so" | grep -q "librexruntime.so" || { echo "no $DLL/libgtav_fast.so"; exit 1; }
defined=$("$NM" -D --defined-only "$DLL/libgtav_fast.so" | grep -c -E " __imp__sub_[0-9A-F]{8}$" || true)
[ "$defined" = 0 ] || { echo "libgtav_fast.so defines $defined game functions: not the shell build"; exit 1; }

RELEASE=1 "$R/tools/android/build_apk.sh" >/dev/null
APK=$R/build/android/apk/gtav-recomp-release.apk
rm -rf "$OUT/link" "$OUT/interface" "$OUT/include" "$OUT/LICENSES" "$OUT/GTAV.apk"
mkdir -p "$OUT/link" "$OUT/interface" "$OUT/include" "$OUT/LICENSES"
cp "$APK" "$OUT/GTAV.apk"
# The link inputs straight out of the APK, so the pack's hash matches what the phone has.
unzip -q -j -o "$APK" lib/arm64-v8a/libgtav_fast.so lib/arm64-v8a/librexruntime.so -d "$OUT/link"
for f in "$OUT/link"/*.so; do
  if strings "$f" | grep -q "$R"; then echo "note: build path in $(basename "$f") (debug strings)"; fi
done
cp "$DLL/game_interface/gtav_hooks.h" "$DLL/game_interface/gtav_game.exports" "$OUT/interface/"
rsync -a --exclude='*.md' --exclude='*.txt' --exclude='.*' "$R/rexglue-sdk/thirdparty/simde/simde/" "$OUT/include/simde/"
cp "$R/rexglue-sdk/thirdparty/simde/COPYING" "$OUT/LICENSES/simde.txt"
cp "$R/rexglue-sdk/thirdparty/sdl3/LICENSE.txt" "$OUT/LICENSES/SDL.txt"
echo "android: $OUT ($(du -sh "$OUT" | cut -f1); APK $(du -sh "$OUT/GTAV.apk" | cut -f1))"
