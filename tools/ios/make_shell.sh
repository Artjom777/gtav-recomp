#!/bin/bash
# The iOS shell for a release: GTAV.app without any game code, unsigned. The Mac app's iPhone step
# (loader/src/install.cpp RunIphone) adds the player's gtav_game.dylib and packs GTAV.ipa, which
# Sideloadly / AltStore sign with the player's Apple ID.
#   tools/ios/make_shell.sh OUT_DIR      -> OUT_DIR/GTAV.app
# Inputs: build/ios-shell (cmake ... CMAKE_SYSTEM_NAME=iOS -DGTAV_GAME_DLL=ON, see docs/IOS_PORT.md),
# the iOS SDK install (rexglue-sdk/out/install/ios-arm64) and a MoltenVK ios-arm64 framework.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$1
BUNDLE_ID="${BUNDLE_ID:-org.gtavrecomp.gtav}"
SHELL_EXE="${GTAV_SHELL:-$R/build/ios-shell/gtav_fast.app/gtav_fast}"
SDK="${GTAV_IOS_SDK:-$R/rexglue-sdk/out/install/ios-arm64}"
MOLTENVK_FW="${MOLTENVK_FW:-/Volumes/Untitled/MacOffload/skate3/moltenvk-ios/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework}"
APP="$OUT/GTAV.app"

rm -rf "$APP"; mkdir -p "$APP"
cp "$SHELL_EXE" "$APP/gtav"
cp "$SDK/lib/librexruntime.dylib" "$SDK/lib/librexgpu-xenos.dylib" "$APP/"
cp "$MOLTENVK_FW/MoltenVK" "$APP/libMoltenVK.dylib"
chmod u+w "$APP"/*.dylib
install_name_tool -id @rpath/libMoltenVK.dylib "$APP/libMoltenVK.dylib" 2>/dev/null
install_name_tool -add_rpath @executable_path "$APP/gtav" 2>/dev/null || true
# No code signature (a sideloading tool signs everything) and no build paths.
for f in "$APP/gtav" "$APP"/*.dylib; do codesign --remove-signature "$f" 2>/dev/null || true; done
if strings "$APP/gtav" "$APP"/lib*.dylib | grep -q "$R"; then echo "build path in a binary: $R"; exit 1; fi
otool -L "$APP/gtav" | grep -q "gtav_game.dylib" || { echo "gtav is not the shell build (no gtav_game.dylib)"; exit 1; }
sed -e "s/@BUNDLE_ID@/$BUNDLE_ID/" -e "s/@BUILD@/$(date +%Y%m%d%H%M)/" "$R/tools/ios/Info.plist" > "$APP/Info.plist"
plutil -convert binary1 "$APP/Info.plist"
printf 'APPL????' > "$APP/PkgInfo"
echo "shell: $APP ($(du -sh "$APP" | cut -f1))"
