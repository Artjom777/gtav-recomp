#!/bin/bash
# clang + lld for the phone (the on-device install compiles the player's game code with them):
# LLVM 19.1.5, the same release the desktop launchers bundle, cross-built with the NDK for
# aarch64-linux-android30, AArch64 backend only, libc++ linked statically (one self-contained
# binary each). The APK carries them as lib/arm64-v8a/libclang.so and libld.lld.so: Android only
# lets an app execute files from its native library folder.
#   tools/android/build_llvm.sh [JOBS]   -> build/android-llvm/out/{clang,lld}
# Memory: about 1.5 GB per job while compiling and ~4 GB while linking clang; check `free` first.
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=19.1.5
JOBS=${1:-3}
W=$ROOT/build/android-llvm
SRC=$W/llvm-project-$VER.src
mkdir -p "$W"
if [ ! -d "$SRC" ]; then
  tarball=$W/llvm-project-$VER.src.tar.xz
  [ -f "$tarball" ] || curl -fL -o "$tarball" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$VER/llvm-project-$VER.src.tar.xz"
  # Printed for the record (compare with the release's .sig / published hashes when it matters).
  sha256sum "$tarball"
  tar -C "$W" -xf "$tarball"
fi
# Host tablegen first (the cross build needs llvm-tblgen / clang-tblgen that run here).
if [ ! -x "$W/host/bin/clang-tblgen" ]; then
  cmake -S "$SRC/llvm" -B "$W/host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DCLANG_ENABLE_ARCMT=OFF \
    -DCLANG_ENABLE_STATIC_ANALYZER=OFF
  nice cmake --build "$W/host" -j"$JOBS" --target llvm-tblgen clang-tblgen
fi
cmake -S "$SRC/llvm" -B "$W/android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android30 \
  -DLLVM_HOST_TRIPLE=aarch64-linux-android \
  -DLLVM_TABLEGEN="$W/host/bin/llvm-tblgen" -DCLANG_TABLEGEN="$W/host/bin/clang-tblgen" \
  -DLLVM_NATIVE_TOOL_DIR="$W/host/bin" \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_UTILS=OFF -DLLVM_INCLUDE_DOCS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
  -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_PLUGIN_SUPPORT=OFF \
  -DLLVM_ENABLE_PLUGINS=OFF -DLLVM_BUILD_TOOLS=OFF -DCLANG_BUILD_TOOLS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -Wl,-z,max-page-size=16384"
nice cmake --build "$W/android" -j"$JOBS" --target clang lld
mkdir -p "$W/out"
STRIP=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
"$STRIP" -o "$W/out/clang" "$W/android/bin/clang-$VER" 2>/dev/null || "$STRIP" -o "$W/out/clang" "$W/android/bin/clang-19"
"$STRIP" -o "$W/out/lld" "$W/android/bin/lld"
ls -la "$W/out"
