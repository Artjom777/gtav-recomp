#!/bin/bash
# Check the C++ Adreno shader passes (build/shader_fix/shader_fix, adreno_shader_fix.cpp) against
# the Python ones on every cached game shader: same HLSL after ubo, same SPIR-V bytes after int64.
#   tools/android/check_shader_fix.sh [JOBS]  -> mismatches listed, a count at the end
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS=$ROOT/build/gtav/tools
WORK=$ROOT/build/android/shader_fix_check
FIX=$ROOT/build/shader_fix/shader_fix
mkdir -p "$WORK"
sed -e 's/(1ull << semanticIndex)/(1u << semanticIndex)/' \
    -e 's/vk::RawBufferLoad<float2>(\(g_PushConstants\.SharedConstants + \)\([0-9]*\))/float2(vk::RawBufferLoad<float>(\1\2), vk::RawBufferLoad<float>(\1\2 + 4))/' \
    "$TOOLS/shader_common.h" > "$WORK/shader_common.h"
one() {
  name=$(basename "$1" .bin); w=$WORK/$name
  "$TOOLS/XenosRecomp" "$1" "$w.py.hlsl" "$WORK/shader_common.h" >/dev/null 2>&1 || { echo "skip $name (xr)"; return; }
  cp "$w.py.hlsl" "$w.cc.hlsl"
  python3 -I "$ROOT/tools/android/ubo_constants.py" "$w.py.hlsl" 2>/dev/null; py=$?
  "$FIX" ubo "$w.cc.hlsl" 2>/dev/null; cc=$?
  if [ $py != $cc ] || ! cmp -s "$w.py.hlsl" "$w.cc.hlsl"; then echo "MISMATCH ubo $name ($py/$cc)"; return; fi
  [ $py = 0 ] || { echo "skip $name (ubo fails in both)"; rm -f "$w".*; return; }
  case $name in ps_*) prof=ps_6_0; extra=;; *) prof=vs_6_0; extra=-fvk-invert-y;; esac
  "$TOOLS/dxc" -T $prof -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout -Wno-ignored-attributes \
    -Qstrip_debug $extra -Fo "$w.spv" "$w.py.hlsl" >/dev/null 2>&1 || { echo "skip $name (dxc)"; rm -f "$w".*; return; }
  python3 -I "$ROOT/tools/android/lower_int64.py" "$w.spv" "$w.py.spv" 2>/dev/null; py=$?
  "$FIX" int64 "$w.spv" "$w.cc.spv" 2>/dev/null; cc=$?
  if [ $py != $cc ] || ! cmp -s "$w.py.spv" "$w.cc.spv"; then echo "MISMATCH int64 $name ($py/$cc)"; return; fi
  echo "ok $name"
  rm -f "$w".*
}
export -f one; export TOOLS WORK FIX ROOT
ls "$ROOT/build/gtav/shader_cache"/*.bin | xargs -P "${1:-3}" -I{} bash -c 'one {}' > "$WORK/result.txt"
grep -c "^ok" "$WORK/result.txt" | sed 's/^/ok: /'; grep -v "^ok" "$WORK/result.txt" | sort | uniq -c | sort -rn | head -20
