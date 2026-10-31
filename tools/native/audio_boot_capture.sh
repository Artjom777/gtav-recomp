#!/bin/sh
# Unattended audio capture of a boot into the world: hidden native window, muted output (the dumps
# still get everything), SECS seconds after the first in-game frame, then the game is closed.
# usage: tools/native/audio_boot_capture.sh NAME [SECS] [game flags...]
# Writes logs/NAME.log, logs/NAME_mix.raw (--audio_dump_raw) and logs/NAME_xma.raw (--xma_dump_raw);
# analyse with tools/native/audio_bursts.py logs/NAME.log.
# GTAV_INPUT_SCRIPT=FILE plays that pad script (--input_script) once the world is loaded, e.g.
# tools/native/input_run_around.txt.
NAME=$1; SECS=${2:-90}; [ $# -ge 2 ] && shift 2 || shift $#
ROOT="$HOME/gtav-linux"
L="$ROOT/logs/$NAME.log"
if pgrep -x gtav >/dev/null; then echo "game already running: not starting"; exit 1; fi
GO="$ROOT/logs/${NAME}_input.txt"
rm -f "$L" "$ROOT/logs/${NAME}_mix.raw" "$ROOT/logs/${NAME}_xma.raw" "$GO"
[ -n "$GTAV_INPUT_SCRIPT" ] && set -- --input_script="$GO" "$@"
"$ROOT/play.sh" "$NAME" --gtav_native_fps --gtav_native_hidden=true --audio_mute=true \
  --audio_dump_raw="$ROOT/logs/${NAME}_mix.raw" --xma_dump_raw="$ROOT/logs/${NAME}_xma.raw" "$@" \
  >"$ROOT/logs/$NAME.stdout" 2>&1 &
PID=$!
S=$(date +%s)
until grep -qE '\[native\] frame [0-9]+: [0-9]{4,} draws' "$L" 2>/dev/null; do
  sleep 5
  if ! kill -0 $PID 2>/dev/null; then echo "game exited before reaching the world"; exit 3; fi
  if [ $(( $(date +%s) - S )) -gt 420 ]; then echo "no in-game frame after 420 s (boot hang?)"; kill -9 $PID; exit 2; fi
done
echo "in game after $(( $(date +%s) - S )) s"
[ -n "$GTAV_INPUT_SCRIPT" ] && sleep 10 && cp "$GTAV_INPUT_SCRIPT" "$GO" && echo "input script started"
sleep "$SECS"
kill -9 $PID 2>/dev/null
sleep 2
ls -la "$ROOT/logs/${NAME}_mix.raw" "$ROOT/logs/${NAME}_xma.raw"
