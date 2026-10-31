#!/bin/bash
# Package build/android/libmain.so + the SDK's shared libraries into a debug-signed APK, without
# Gradle: javac (SDL's Java + GameActivity) -> d8 -> aapt2 link -> zip libs -> zipalign -> apksigner.
# Usage: tools/android/build_apk.sh [install]   -> build/android/apk/gtav-recomp.apk
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

"$BT/aapt2" link -o base.apk --manifest "$APP/AndroidManifest.xml" -I "$JAR" \
  --min-sdk-version 30 --target-sdk-version 34

cp "$ROOT/build/android/libmain.so" "$SDK_LIB/librexruntime.so" "$SDK_LIB/librexgpu-xenos.so" \
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
  --out gtav-recomp.apk aligned.apk
rm -f unaligned.apk aligned.apk base.apk
ls -la gtav-recomp.apk
if [ "$1" = install ]; then
  adb install -r gtav-recomp.apk
fi
