#!/bin/sh
# Unattended perf run: hidden native window (mode on), SECS seconds after the first in-game frame,
# then print the renderer's timing lines and close the game. usage: night_perf.sh NAME [SECS] [game flags...]
NAME=$1; SECS=${2:-180}; shift 2 2>/dev/null
L=/e/gtav-native/logs/boot/$NAME.log
if tasklist | grep -qi gtav_fast; then echo "game already running: not starting"; exit 1; fi
rm -f $L
powershell -NoProfile -ExecutionPolicy Bypass -File 'E:\gtav-native\play.ps1' -Log $NAME -Fps 0 -Counter --gtav_native_hidden=true --audio_mute=true "$@" >/dev/null
S=$(date +%s)
until grep -qE '\[native\] frame [0-9]+: [0-9]{4,} draws' $L 2>/dev/null; do
  sleep 5
  if [ $(( $(date +%s) - S )) -gt 300 ]; then echo "no in-game frame after 300 s (boot hang?)"; taskkill //F //IM gtav_fast.exe >/dev/null 2>&1; exit 2; fi
done
echo "in game after $(( $(date +%s) - S )) s"
sleep $SECS
taskkill //F //IM gtav_fast.exe >/dev/null 2>&1
grep -E 'record ms per frame|textures per frame|occlusion queries|presented [0-9]+ frames|waited for the renderer|preloaded|audio:' $L | tail -24 | cut -c26-260
