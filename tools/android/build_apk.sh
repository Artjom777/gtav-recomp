#!/bin/bash
# Package build/android/libmain.so + the SDK's shared libraries into a debug-signed APK, without
# Gradle: javac (SDL's Java + GameActivity) -> d8 -> aapt2 link -> zip libs -> zipalign -> apksigner.
# Usage: tools/android/build_apk.sh [install]   -> build/android/apk/gtav-recomp.apk
# RELEASE=1: the public APK with no game code (build/android-dll, GTAV_GAME_DLL=ON: the libmain.so
# shim + libgtav_fast.so; the player's libgtav_game.so comes from the PC's Build for Android), not
# debuggable -> build/android/apk/gtav-recomp-release.apk
set -e
if [ -f "$HOME/android-tools/env.sh" ]; then
  source "$HOME/android-tools/env.sh"
fi
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP=$ROOT/tools/android/app
OUT=${OUT:-"$ROOT/build/android/apk"}
SDK_LIB=${SDK_LIB:-"$ROOT/rexglue-sdk/out/install/android-arm64/lib"}
SDL_JAVA=${SDL_JAVA:-"$ROOT/rexglue-sdk/thirdparty/sdl3/android-project/app/src/main/java"}

if [ -z "${BT:-}" ] || [ ! -d "${BT:-}" ]; then
  BT=$(find "$ANDROID_HOME/build-tools" -maxdepth 1 -mindepth 1 2>/dev/null | sort -V | tail -n 1)
fi
if [ -z "${JAR:-}" ] || [ ! -f "${JAR:-}" ]; then
  JAR=$(find "$ANDROID_HOME/platforms" -name "android.jar" 2>/dev/null | sort -V | tail -n 1)
fi
if [ -z "${NDK_LIBCXX:-}" ] || [ ! -f "${NDK_LIBCXX:-}" ]; then
  NDK_LIBCXX=$(find "$ANDROID_NDK_HOME" -name "libc++_shared.so" 2>/dev/null | grep "aarch64" | head -n 1)
fi
if [ -z "${LLVM_STRIP:-}" ] || [ ! -x "${LLVM_STRIP:-}" ]; then
  LLVM_STRIP=$(find "$ANDROID_NDK_HOME" -name "llvm-strip" 2>/dev/null | head -n 1)
  [ -z "$LLVM_STRIP" ] && LLVM_STRIP="llvm-strip"
fi
KEY=${KEY:-"$HOME/android-tools/debug.keystore"}
mkdir -p "$(dirname "$KEY")"

mkdir -p "$OUT"
cd "$OUT"
rm -rf classes stage && mkdir -p classes stage/lib/arm64-v8a

javac --release 17 -nowarn -classpath "$JAR" -d classes \
  $(find "$SDL_JAVA" "$APP/java" -name '*.java') 2>&1 | grep -v "^Note:" || true
"$BT/d8" --release --min-api 30 --lib "$JAR" --output stage $(find classes -name '*.class')

MANIFEST=$APP/AndroidManifest.xml
NAME=${APK_NAME:-gtav-recomp}
# The setup screen's library (SetupActivity) is in both: it has no game code.
LIBS=("$ROOT/build/android/libmain.so" "$ROOT/build/android-dll/libgtav_setup.so")
if [ "${RELEASE:-0}" = 1 ]; then
  NAME=${APK_NAME:-gtav-recomp-release}
  LIBS=("$ROOT/build/android-dll/libmain.so" "$ROOT/build/android-dll/libgtav_fast.so"
        "$ROOT/build/android-dll/libgtav_setup.so")
  sed 's/ *android:debuggable="true"//' "$APP/AndroidManifest.xml" > "$OUT/AndroidManifest.release.xml"
  MANIFEST=$OUT/AndroidManifest.release.xml
fi
if [ -n "${LIBS_OVERRIDE:-}" ]; then
  read -r -a LIBS <<< "$LIBS_OVERRIDE"
fi
"$BT/aapt2" link -o base.apk --manifest "$MANIFEST" -I "$JAR" \
  --min-sdk-version 30 --target-sdk-version 34

cp "${LIBS[@]}" "$SDK_LIB/librexruntime.so" "$SDK_LIB/librexgpu-xenos.so" \
   "$NDK_LIBCXX" stage/lib/arm64-v8a/
"$LLVM_STRIP" --strip-debug stage/lib/arm64-v8a/*.so
cp base.apk unaligned.apk
(cd stage && zip -q -X ../unaligned.apk classes.dex && zip -q -X -0 -r ../unaligned.apk lib)

"$BT/zipalign" -f -P 16 4 unaligned.apk aligned.apk
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android \
  --out "$NAME.apk" aligned.apk
rm -f unaligned.apk aligned.apk base.apk
ls -la "$NAME.apk"

echo "=== Packaged native libraries in $NAME.apk: ==="
unzip -l "$NAME.apk" | grep 'lib/arm64-v8a/' || true

mkdir -p "$OUT/libs/$NAME"
cp stage/lib/arm64-v8a/*.so "$OUT/libs/$NAME/"

if [ "$1" = install ]; then
  adb install -r $NAME.apk
fi
