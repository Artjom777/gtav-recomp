#!/bin/sh
# bin_ab.sh <name> <binA> <binB> [rounds]: alternate headless runs of two game binaries (files in
# build/gtav, copied over build/gtav/gtav per run) and print fps / record / GPU over the same
# window of each run (acquire lines 10-40 after the world loads).
R="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$R" || exit 1
N=$1; A=$2; B=$3; ROUNDS=${4:-2}
cp build/gtav/gtav build/gtav/gtav.bin_ab_saved
i=0
while [ $i -lt $ROUNDS ]; do
  for bin in $A $B; do
    run=${N}_${bin}_$i
    cp build/gtav/$bin build/gtav/gtav
    SHOT_SECS=300 tools/native/headless_run.sh $run >/dev/null
    until [ $(grep -c "avg acquire" logs/$run.log) -ge 42 ]; do sleep 3; pgrep -x gtav >/dev/null || break; done
    gpu=$(grep 'avg acquire' logs/$run.log | sed -n '10,40p' | grep -o 'GPU frame [0-9.]*' | awk '{s+=$3;n++} END{printf "%.2f", s/n}')
    fr=$(python3 tools/native/fps_from_log.py logs/$run.log | awk 'NR>3 && /fps/{f+=$3; r+=$5; n++} END{printf "fps %.1f record %.2f", f/n, r/n}')
    echo "$run: $fr GPU $gpu"
    pkill -x gtav; sleep 2
  done
  i=$((i+1))
done
cp build/gtav/gtav.bin_ab_saved build/gtav/gtav
