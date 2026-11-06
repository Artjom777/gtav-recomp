#!/bin/bash
# Cross-compile the ReXGlue SDK (runtime libraries) for Android arm64-v8a with the NDK.
# Usage: tools/android/build_sdk.sh [configure|build|install|all]   (default: all)
set -e
if [ -f "$HOME/android-tools/env.sh" ]; then
  source "$HOME/android-tools/env.sh"
fi
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SDK=$ROOT/rexglue-sdk
BUILD=${BUILD:-"$SDK/out/build/android-arm64"}
PREFIX=${PREFIX:-"$SDK/out/install/android-arm64"}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
MARCH=${MARCH:-"armv8.2-a+lse+rcpc+dotprod"}
step=${1:-all}
if [ "$step" = configure ] || [ "$step" = all ]; then
  cmake -S "$SDK" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23 \
    -DCMAKE_C_FLAGS="-march=${MARCH}" -DCMAKE_CXX_FLAGS="-march=${MARCH} -fno-semantic-interposition" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-Bsymbolic-functions -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF
fi
if [ "$step" = build ] || [ "$step" = all ]; then
  cmake --build "$BUILD" -j"$JOBS"
fi
if [ "$step" = install ] || [ "$step" = all ]; then
  cmake --install "$BUILD"
  # The install registers its prefix in ~/.cmake/packages; a host configure must never find it.
  mkdir -p "$BUILD"
  grep -l -E "android" ~/.cmake/packages/rexglue/* 2>/dev/null | xargs -r -I{} mv {} "$BUILD/registry-entry" || true
fi
