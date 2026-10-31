#!/bin/sh
# headless_run.sh <name> [flags]: benchmark run without a window (macOS): screenshots every
# SHOT_SECS (default 30) to /Volumes/Untitled/gtav-captures/<name>, log logs/<name>.log, scripted
# pad from logs/<name>.pad once that file exists (see --input_script). Returns once the open world
# renders (or the game exited). PASS_STATS=1: GTAV_NATIVE_PASS_STATS.
R="$(cd "$(dirname "$0")/../.." && pwd)"
N=$1; shift
cd "$R" || exit 1
[ -n "$PASS_STATS" ] && export GTAV_NATIVE_PASS_STATS=1
pkill -x gtav; sleep 1
rm -f logs/$N.pad; mkdir -p /Volumes/Untitled/gtav-captures/$N
GTAV_NATIVE_SHOT_DIR=/Volumes/Untitled/gtav-captures/$N GTAV_SHOT_SECS=${SHOT_SECS:-30} GTAV_IGNORE_SETTINGS=1 \
  GTAV_NATIVE=on nohup ./play_mac.sh $N --gtav_native_hidden=true --fullscreen=false \
  --input_script="$R/logs/$N.pad" "$@" > logs/$N.stdout 2>&1 &
sleep 5
until grep -qE "per frame 1[2-9][0-9] render passes" logs/$N.log 2>/dev/null || ! pgrep -x gtav >/dev/null; do sleep 2; done
pgrep -x gtav >/dev/null && echo "world loaded" || { echo "EXITED"; tail -3 logs/$N.log; }
