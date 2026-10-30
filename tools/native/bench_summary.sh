#!/bin/sh
# One-line summary of a bench log: mean fps over the last N fps lines, worst frame, mean draws,
# and in-game hitches (>= 90 ms with >100 draws) with how many fell on a frame divisible by 64.
# usage: tools/native/bench_summary.sh LOG [N=60]
L=$1; N=${2:-60}
grep -E "\[native\] fps=" "$L" | tail -"$N" | sed -E 's/.*fps=([0-9.]+).*max=([0-9.]+)ms.*drawidx=([0-9]+).*/\1 \2 \3/' |
  awk '{f+=$1; if($2>m)m=$2; d+=$3; n++} END{printf "%s: avg %.1f fps, worst frame %.0f ms, %.0f draws (%d s)\n", "'"$(basename "$L")"'", f/n, m, d/n, n}'
grep -oE "hitch [0-9.]+ ms at guest frame [0-9]+: record [0-9.]+ ms, wait [0-9.]+ ms, [0-9]+ new pipelines, [0-9]+ textures, [0-9]+ draws" "$L" |
  awk '{ms=$2; fr=$7; sub(":","",fr); dr=$20; if (ms>=90 && dr>100) {n++; s+=ms; if (fr%64==0) z++}} END{printf "  in-game hitches >= 90 ms: %d (mean %.0f ms), on frame%%64==0: %d\n", n, n?s/n:0, z}'
