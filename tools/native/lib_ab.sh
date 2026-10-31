#!/bin/sh
# lib_ab.sh <name> <suffixA> <suffixB> [rounds]: like bin_ab.sh for the SDK runtime: alternates
# build/gtav/librex{runtime,gpu-xenos}.<suffix>.dylib; prints load time, fps, record, GPU.
R="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$R" || exit 1
N=$1; A=$2; B=$3; ROUNDS=${4:-2}; i=0
while [ $i -lt $ROUNDS ]; do
  for v in $A $B; do
    run=${N}_${v}_$i
    for l in librexruntime librexgpu-xenos; do cp build/gtav/$l.$v.dylib build/gtav/$l.dylib; done
    SHOT_SECS=300 tools/native/headless_run.sh $run >/dev/null
    until [ $(grep -c "avg acquire" logs/$run.log) -ge 42 ]; do sleep 3; pgrep -x gtav >/dev/null || break; done
    gpu=$(grep 'avg acquire' logs/$run.log | sed -n '10,40p' | grep -o 'GPU frame [0-9.]*' | awk '{s+=$3;n++} END{printf "%.2f", s/n}')
    fr=$(python3 tools/native/fps_from_log.py logs/$run.log | awk 'NR>3 && /fps/{f+=$3; r+=$5; d+=$NF; n++} END{printf "fps %.1f record %.2f draws %.0f", f/n, r/n, d/n}')
    t0=$(head -1 logs/$run.log | cut -c2-24); t1=$(grep -m1 -E "per frame 1[2-9][0-9] render passes" logs/$run.log | cut -c2-24)
    load=$(python3 -c "import datetime as d;f='%Y-%m-%d %H:%M:%S.%f';print((d.datetime.strptime('$t1',f)-d.datetime.strptime('$t0',f)).total_seconds())")
    echo "$run: load $load s, $fr, GPU $gpu"
    pkill -x gtav; sleep 2
  done
  i=$((i+1))
done
