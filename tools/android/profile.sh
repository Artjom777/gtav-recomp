#!/bin/bash
# Flat CPU profile of the running game on the phone: per thread and per function (simpleperf).
#   tools/android/profile.sh NAME [WARMUP_SECS] [SECS] [flags/env: for args.txt ...]
# Starts the game with the flags, waits WARMUP_SECS (default 120, the drive from the autosave is
# loaded by then), samples SECS (default 20), stops the game and writes logs/android/NAME.perf.txt
# (top threads, then the top symbols of the render and main game threads) and NAME.log.
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
name=$1 warm=${2:-120} secs=${3:-20}
shift 3 2>/dev/null || shift $#
[ -n "$name" ] || { echo "usage: $0 NAME [WARMUP] [SECS] [flags...]"; exit 1; }
adb wait-for-device
"$ROOT/tools/android/set_args.sh" "$@"
KEEP=1 "$ROOT/tools/android/run.sh" "$warm" 0 >/dev/null
adb shell "simpleperf record --app $PKG -e cpu-clock -f 2000 --duration $secs -o /data/local/tmp/gtav.perf" >/dev/null 2>&1 ||
  adb shell "run-as $PKG simpleperf record --app $PKG -e cpu-clock -f 2000 --duration $secs -o /data/local/tmp/gtav.perf"
adb shell am force-stop $PKG
mkdir -p "$ROOT/logs/android"
adb pull "$D/logs/gtav.log" "$ROOT/logs/android/$name.log" >/dev/null
out=$ROOT/logs/android/$name.perf.txt
{
  echo "== threads"
  adb shell "simpleperf report -i /data/local/tmp/gtav.perf --sort tid,comm --percent-limit 1" 2>/dev/null | sed -n '/Overhead/,$p'
  for tid in $(adb shell "simpleperf report -i /data/local/tmp/gtav.perf --sort tid --percent-limit 4" 2>/dev/null |
               sed -n '/Overhead/,$p' | awk 'NR>1 && $2 ~ /^[0-9]+$/ {print $2}' | head -4); do
    echo "== thread $tid"
    adb shell "simpleperf report -i /data/local/tmp/gtav.perf --tids $tid --sort dso,symbol --percent-limit 0.7" 2>/dev/null |
      sed -n '/Overhead/,$p' | cut -c1-170
  done
} > "$out"
head -60 "$out"
