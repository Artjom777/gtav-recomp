#!/bin/bash
# Regenerate the game's SPIR-V for Android from the shader cache (build/gtav/shader_cache/*.bin,
# the player's own shaders) with XenosRecomp + dxc on this host, using an Adreno-safe copy of
# shader_common.h: the Adreno compiler (EV031.36) asserts on 64-bit shifts ("srcOPME->getSize() ==
# 2"), and tfetchTexcoord shifted 1ull. Also makes the sun-tile variant (shader_compiler.cpp kVariants).
# Output: build/android/spirv (push to files/spirv). Usage: tools/android/make_spirv.sh [JOBS]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS=$ROOT/build/gtav/tools
CACHE=$ROOT/build/gtav/shader_cache
OUT=$ROOT/build/android/spirv
WORK=$ROOT/build/android/spirv_work
mkdir -p "$OUT" "$WORK"
# Adreno also asserts on 2-component loads through a device address: split float2 loads in two.
sed -e 's/(1ull << semanticIndex)/(1u << semanticIndex)/' \
    -e 's/vk::RawBufferLoad<float2>(\(g_PushConstants\.SharedConstants + \)\([0-9]*\))/float2(vk::RawBufferLoad<float>(\1\2), vk::RawBufferLoad<float>(\1\2 + 4))/' \
    "$TOOLS/shader_common.h" > "$WORK/shader_common.h"
grep -q '1u << semanticIndex' "$WORK/shader_common.h"
touch "$WORK/shader_common.h"  # regenerate everything when this script runs
! grep -q 'RawBufferLoad<float2>' "$WORK/shader_common.h"
one() {
  bin=$1; name=$(basename "$bin" .bin); out=$OUT/$name.spv
  [ -s "$out" ] && [ "$out" -nt "$WORK/shader_common.h" ] && return 0
  hlsl=$WORK/$name.hlsl
  "$TOOLS/XenosRecomp" "$bin" "$hlsl" "$WORK/shader_common.h" >/dev/null 2>&1 || { echo "xr fail $name"; return 0; }
  # Constants from uniform buffers (set 4) instead of device-address loads: ubo_constants.py.
  python3 -I "$UBO" "$hlsl" || { echo "ubo fail $name"; return 0; }
  case $name in ps_*) prof=ps_6_0; extra=;; *) prof=vs_6_0; extra=-fvk-invert-y;; esac
  "$TOOLS/dxc" -T $prof -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout -Wno-ignored-attributes \
    -Qstrip_debug $extra -Fo "$out" "$hlsl" >/dev/null 2>&1 || echo "dxc fail $name"
  # No shaderInt64 on Adreno (Vulkan 1.1 driver): 64-bit integers -> uvec2 (lower_int64.py).
  python3 -I "$LOWER" "$out" "$out" || echo "lower fail $name"
  if [ "$prof" = vs_6_0 ] && grep -q '= vfetchIndirect(' "$hlsl"; then : > "$OUT/$name.indirect"; fi
  if [ "$name" = ps_D258EBE32001470C ]; then
    sed -e 's/r0.y = select(r0.y > 0.0, c255.x, c255.y);/r0.y = c255.x;/' \
        -e "s/r0.w = c255.w >= r0.x;/r0.w = ${TILES_SKY_TEST:-c255.w >= r0.x};/" \
        -e 's/oC0.z = select(r0.w > 0.0, r1.x, c255.z);/oC0.z = select(r0.w > 0.0, max(r1.x, 0.0625), c255.z);/' \
        "$hlsl" > "$WORK/${name}_tiles.hlsl"
    "$TOOLS/dxc" -T ps_6_0 -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout -Wno-ignored-attributes \
      -Qstrip_debug -Fo "$OUT/${name}_tiles2.spv" "$WORK/${name}_tiles.hlsl" >/dev/null 2>&1 || echo "dxc fail tiles"
    python3 -I "$LOWER" "$OUT/${name}_tiles2.spv" "$OUT/${name}_tiles2.spv" || echo "lower fail tiles"
  fi
  rm -f "$hlsl"
}
LOWER=$ROOT/tools/android/lower_int64.py
UBO=$ROOT/tools/android/ubo_constants.py
export -f one; export TOOLS OUT WORK LOWER UBO
ls "$CACHE"/*.bin | xargs -P "${1:-8}" -I{} bash -c 'one {}'
echo "spirv: $(ls "$OUT"/*.spv | wc -l) files in $OUT"
