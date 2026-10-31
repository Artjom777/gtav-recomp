#!/bin/bash
# Live A/B of one runtime cvar on the phone (same scenes for both sides, see --gtav_native_ab):
#   tools/android/ab.sh NAME 'cvar=a|b' [SECS] [--extra=flag ...] [env:NAME=V ...]
# Waits for the phone to cool (battery < AB_MAX_TEMP tenths of a degree C, default 360), runs the
# game for SECS (default 360) from the autosave, saves the log to logs/android/NAME.log and prints
# the A/B lines plus the GPU clock and battery temperature at the end.
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
name=$1 spec=$2 secs=${3:-360}
shift 3 2>/dev/null || shift $#
[ -n "$name" ] && [ -n "$spec" ] || { echo "usage: $0 NAME 'cvar=a|b' [SECS] [flags...]"; exit 1; }
temp() { adb shell dumpsys battery | awk '/temperature/ {print $2}'; }
gpu_mhz() { adb shell cat /sys/class/kgsl/kgsl-3d0/gpuclk 2>/dev/null | awk '{printf "%d", $1 / 1000000}'; }
for i in $(seq 60); do
  t=$(temp)
  [ "${t:-0}" -lt "${AB_MAX_TEMP:-360}" ] && break
  [ "$i" = 1 ] && echo "cooling: battery $t"
  sleep 10
done
"$ROOT/tools/android/set_args.sh" "--gtav_native_ab=$spec" "$@"
echo "start: battery $(temp), GPU $(gpu_mhz) MHz"
KEEP=1 "$ROOT/tools/android/run.sh" "$secs" 0 >/dev/null
echo "end: battery $(temp), GPU $(gpu_mhz) MHz, pid $(adb shell pidof $PKG)"
adb shell am force-stop $PKG
mkdir -p "$ROOT/logs/android"
adb pull "$D/logs/gtav.log" "$ROOT/logs/android/$name.log" >/dev/null
adb logcat -d | grep -E "F DEBUG|F libc" | grep -v "Access denied" | head -20
grep -E "\[native\] A/B" "$ROOT/logs/android/$name.log" | cut -c1-260
