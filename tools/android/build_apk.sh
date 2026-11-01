#!/bin/bash
# Package build/android/libmain.so + the SDK's shared libraries into a debug-signed APK, without
# Gradle: javac (SDL's Java + GameActivity) -> d8 -> aapt2 link -> zip libs -> zipalign -> apksigner.
# Usage: tools/android/build_apk.sh [install]   -> build/android/apk/gtav-recomp.apk
# RELEASE=1: the public APK with no game code (build/android-dll, GTAV_GAME_DLL=ON: the libmain.so
# shim + libgtav_fast.so; the player's libgtav_game.so comes from the PC's Build for Android), not
# debuggable -> build/android/apk/gtav-recomp-release.apk
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP=$ROOT/tools/android/app
OUT=$ROOT/build/android/apk
SDK_LIB=$ROOT/rexglue-sdk/out/install/android-arm64/lib
SDL_JAVA=$ROOT/rexglue-sdk/thirdparty/sdl3/android-project/app/src/main/java
BT=$ANDROID_HOME/build-tools/36.0.0
JAR=$ANDROID_HOME/platforms/android-36/android.jar
NDK_LIBCXX=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so
KEY=$HOME/android-tools/debug.keystore

mkdir -p "$OUT"
cd "$OUT"
rm -rf classes stage && mkdir -p classes stage/lib/arm64-v8a

javac --release 17 -nowarn -classpath "$JAR" -d classes \
  $(find "$SDL_JAVA" "$APP/java" -name '*.java') 2>&1 | grep -v "^Note:" || true
"$BT/d8" --release --min-api 30 --lib "$JAR" --output stage $(find classes -name '*.class')

MANIFEST=$APP/AndroidManifest.xml
NAME=gtav-recomp
# The setup screen's library (SetupActivity) is in both: it has no game code.
LIBS=("$ROOT/build/android/libmain.so" "$ROOT/build/android-dll/libgtav_setup.so")
if [ "${RELEASE:-0}" = 1 ]; then
  NAME=gtav-recomp-release
  LIBS=("$ROOT/build/android-dll/libmain.so" "$ROOT/build/android-dll/libgtav_fast.so"
        "$ROOT/build/android-dll/libgtav_setup.so")
  sed 's/ *android:debuggable="true"//' "$APP/AndroidManifest.xml" > "$OUT/AndroidManifest.release.xml"
  MANIFEST=$OUT/AndroidManifest.release.xml
fi
"$BT/aapt2" link -o base.apk --manifest "$MANIFEST" -I "$JAR" \
  --min-sdk-version 30 --target-sdk-version 34

cp "${LIBS[@]}" "$SDK_LIB/librexruntime.so" "$SDK_LIB/librexgpu-xenos.so" \
   "$NDK_LIBCXX" stage/lib/arm64-v8a/
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-debug stage/lib/arm64-v8a/*.so
cp base.apk unaligned.apk
(cd stage && zip -q -X ../unaligned.apk classes.dex && zip -q -X -0 -r ../unaligned.apk lib)

"$BT/zipalign" -f -P 16 4 unaligned.apk aligned.apk
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android \
  --out $NAME.apk aligned.apk
rm -f unaligned.apk aligned.apk base.apk
ls -la $NAME.apk
if [ "$1" = install ]; then
  adb install -r $NAME.apk
fi
