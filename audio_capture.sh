#!/bin/sh
# Audio capture (as audio_capture.cmd): play normally, press F10 right after each weird loud noise
# (marks it in logs/audio.log). The device mix goes to logs/audio_capture.raw (float32 stereo 48 kHz);
# then: python3 tools/native/audio_marks.py logs/audio.log logs/audio_capture.raw
exec "$(dirname "$0")/play.sh" audio --gtav_native_fps --audio_dump_raw="$HOME/gtav-linux/logs/audio_capture.raw" "$@"
