#!/bin/bash
# Cross-compile the game (libmain.so) for Android arm64-v8a against the Android SDK install
# (tools/android/build_sdk.sh). Usage: tools/android/build_game.sh [configure|build|all]
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$ROOT/build/android
JOBS=${JOBS:-4}
step=${1:-all}
if [ "$step" = configure ] || [ "$step" = all ]; then
  cmake -S "$ROOT/gtav" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_FIND_ROOT_PATH="$ROOT/rexglue-sdk/out/install/android-arm64" \
    -Drexglue_DIR="$ROOT/rexglue-sdk/out/install/android-arm64/lib/cmake/rexglue" \
    -DSDL3_DIR="$ROOT/rexglue-sdk/out/install/android-arm64/lib/cmake/SDL3" \
    -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON
fi
if [ "$step" = build ] || [ "$step" = all ]; then
  cmake --build "$BUILD" -j"$JOBS"
fi
