#!/bin/bash
# Push tools/android/args.txt plus any extra flags given here to the phone's files/args.txt.
# Usage: tools/android/set_args.sh [--flag=value ...]   (no extra flags = the defaults)
source ~/android-tools/env.sh
T=$(mktemp)
cat "$(dirname "$0")/args.txt" > "$T"
for a in "$@"; do echo "$a" >> "$T"; done
adb push "$T" /sdcard/Android/data/com.gtavrecomp.app/files/args.txt >/dev/null
adb shell chmod a+rw /sdcard/Android/data/com.gtavrecomp.app/files/args.txt
rm -f "$T"
