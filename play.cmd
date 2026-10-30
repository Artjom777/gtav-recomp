@echo off
rem Play GTA V (native renderer, uncapped). In the game window: F9 = dump this frame for analysis, F10 = screenshot.
rem (--gtav_audio_unsync=false would cap it at 62.5 fps: the update thread then waits for every 16 ms audio frame.)
powershell -NoProfile -ExecutionPolicy Bypass -File E:\gtav-native\play.ps1 -Log play -Fps 0 -Counter
