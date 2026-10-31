#!/bin/sh
# live_shot.sh <run name> <out.png> [cvar=value ...]: sets the cvars live (logs/menu_set), waits
# for two fresh headless screenshots and saves the last one as a 640x360 PNG.
R="$(cd "$(dirname "$0")/../.." && pwd)"
D=/Volumes/Untitled/gtav-captures/$1; OUT=$2; shift 2
cd "$R" || exit 1
for kv in "$@"; do
  echo "$kv" > build/gtav/logs/menu_set
  until [ ! -f build/gtav/logs/menu_set ]; do sleep 0.3; done
done
for i in 1 2; do f0=$(ls -t $D | head -1); until [ "$(ls -t $D | head -1)" != "$f0" ]; do sleep 0.3; done; done
sleep 1; f=$(ls -t $D | head -1)
python3 -c "from PIL import Image; Image.open('$D/$f').resize((640,360)).save('$OUT')"; echo "$OUT <- $f"
