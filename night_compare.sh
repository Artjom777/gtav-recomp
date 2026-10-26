#!/bin/sh
# Unattended native-vs-emulator comparison: hidden shadow-mode run, file-triggered dump DELAY s after
# the first in-game frame, wait for the emulated window, compare_raw, close the game.
# usage: night_compare.sh NAME [DELAY]
NAME=$1; DELAY=${2:-45}
L=/e/gtav-native/logs/boot/$NAME.log
taskkill //F //IM gtav_fast.exe >/dev/null 2>&1; sleep 3
powershell -NoProfile -ExecutionPolicy Bypass -File 'E:\gtav-native\play.ps1' -Log $NAME -Fps 30 -Mode shadow --gtav_native_hidden=true >/dev/null
until [ "$(grep -cE '\[native\] frame [0-9]+: [0-9]{3,} draws' $L 2>/dev/null)" -ge 1 ] 2>/dev/null; do sleep 5; done
sleep $DELAY
touch /e/gtav-native/build/logs/dump_request
until grep -q 'dumped' $L; do sleep 5; done
N=$(grep 'dumped' $L | tail -1 | sed 's/.*frame \([0-9]*\) to.*/\1/')
S=$(date +%s)
until [ -d /e/gtav-native/build/logs/emu_dump/frame_$((N+4)) ] || [ $(( $(date +%s) - S )) -gt 2400 ]; do sleep 15; done
sleep 20
taskkill //F //IM gtav_fast.exe >/dev/null 2>&1
cd /e/gtav-native && python tools/native/compare_raw.py build/logs/native_dump/frame_$N build/logs/emu_dump > logs/$NAME.compare.txt 2>&1
echo "frame $N"; grep -v 'no emulated\|undecodable' logs/$NAME.compare.txt | head -40
