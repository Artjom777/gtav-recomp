#!/bin/bash
# Restart the game on the phone, wait SECS (default 30), then print any native crash and the tail
# of files/logs/gtav.log. Usage: tools/android/run.sh [SECS] [LOG_LINES]
source ~/android-tools/env.sh
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
adb shell am force-stop $PKG
adb logcat -c
adb shell "mv -f $D/logs/gtav.log $D/logs/prev.log 2>/dev/null"
adb shell am start -n $PKG/.GameActivity >/dev/null
sleep "${1:-30}"
echo "pid: $(adb shell pidof $PKG)"
adb logcat -d | grep -E "F DEBUG|F libc" | grep -vE "Access denied" | cut -c1-220 | head -${CRASH_LINES:-40}
adb shell "tail -n ${2:-40} $D/logs/gtav.log" | cut -c1-240
# Stop the game afterwards (it heats the phone and throttles the next measurement) unless KEEP=1.
[ "${KEEP:-0}" = 1 ] || adb shell am force-stop $PKG
