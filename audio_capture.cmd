@echo off
rem Audio capture: play normally, press F10 right after each weird loud noise (marks it in logs\boot\audio.log).
rem The device mix is recorded to logs\audio_capture.raw (float32 stereo 48 kHz); tools\native\audio_marks.py cuts out each marked moment.
powershell -NoProfile -ExecutionPolicy Bypass -File E:\gtav-native\play.ps1 -Log audio -Fps 0 -Counter --audio_dump_raw=E:/gtav-native/logs/audio_capture.raw
