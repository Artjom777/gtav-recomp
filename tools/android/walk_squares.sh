#!/bin/bash
# Wrongly lit 16x16 tiles while walking: starts the game with the given flags, walks forward while
# turning the camera for 40 s (--input_script), captures a frame every 2 s and counts the bad tiles
# in each (tools/native/count_squares.py).
#   tools/android/walk_squares.sh NAME [--ab 'cvar=a|b'] [flags/env: ...]
#     -> logs/android/NAME_walk/, NAME_walk.txt
# With --ab the cvar flips every 60 frames during the same walk (--gtav_native_ab), a frame is
# captured every second and the two sides are counted apart (frames named *_ab0 / *_ab1).
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
name=$1
shift
ab=
if [ "$1" = --ab ]; then ab=$2; shift 2; fi
walk=${WALK_SECS:-40}
secs=2
extra=()
if [ -n "$ab" ]; then secs=1; extra=("--gtav_native_ab=$ab"); fi
out=$ROOT/logs/android/${name}_walk
rm -rf "$out"
mkdir -p "$out"
printf '# start dur buttons lt rt lx ly rx ry\n0.00 %d.00 0000 0 0 0 32767 9000 0\n' "$walk" > "$out/move.txt"
adb wait-for-device
adb shell "rm -f $D/logs/native_*.ppm $D/logs/move.txt"
"$ROOT/tools/android/set_args.sh" "--input_script=$D/logs/move.txt" "env:GTAV_NATIVE_SHOT_DIR=$D/logs" \
  env:GTAV_SHOT_SECS=$secs "${extra[@]}" "$@" >/dev/null
KEEP=1 "$ROOT/tools/android/run.sh" 105 0 >/dev/null
shots_before=$(adb shell "ls $D/logs/ | grep -c ppm")
adb push "$out/move.txt" $D/logs/move.txt >/dev/null
sleep $((walk + 2))
adb shell am force-stop $PKG
# Only the frames taken during the walk.
for f in $(adb shell "ls $D/logs/ | grep ppm" | tail -n +$((shots_before + 1))); do
  adb pull "$D/logs/$f" "$out/" >/dev/null
done
adb pull "$D/logs/gtav.log" "$out/gtav.log" >/dev/null
py=$HOME/.venvs/reskate/bin/python3
txt=$ROOT/logs/android/${name}_walk.txt
if [ -n "$ab" ]; then
  : > "$txt"
  for side in 0 1; do
    ls "$out"/*_ab$side.ppm >/dev/null 2>&1 || continue
    echo "side $side (${ab#*=} value $((side + 1))): $("$py" "$ROOT/tools/native/count_squares.py" "$out"/*_ab$side.ppm | tail -1)" | tee -a "$txt"
  done
else
  "$py" "$ROOT/tools/native/count_squares.py" "$out"/*.ppm > "$txt"
  tail -1 "$txt"
fi
