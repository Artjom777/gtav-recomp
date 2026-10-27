@echo off
rem Play GTA V (native renderer, uncapped; the audio sync caps it at ~62 fps: unsynced audio is the suspect for loud noises and the slow Wasted screen). In the game window: F9 = dump this frame for analysis, F10 = screenshot.
powershell -NoProfile -ExecutionPolicy Bypass -File E:\gtav-native\play.ps1 -Log play -Fps 0 -Counter --gtav_audio_unsync=false
