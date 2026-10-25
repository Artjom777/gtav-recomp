#!/bin/sh
# Snapshot every thread's host stack in the running game (recompiled guest functions show as
# sub_XXXXXXXX = guest address). Takes several samples so spinning threads stand out.
# Usage: hangdump.sh [name] [samples]   -> ~/gtav-linux/logs/<name>_N.txt
name=${1:-hang}; n=${2:-3}
pid=$(pgrep -x gtav) || { echo "gtav not running"; exit 1; }
out="$HOME/gtav-linux/logs"
for i in $(seq 1 "$n"); do
  gdb -q -batch -p "$pid" \
    -ex 'set pagination off' -ex 'set print frame-arguments none' \
    -ex 'info threads' -ex 'thread apply all bt 40' > "$out/${name}_$i.txt" 2>&1
  echo "$out/${name}_$i.txt"
  sleep 2
done
