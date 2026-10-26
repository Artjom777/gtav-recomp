@echo off
rem Play GTA V (native renderer, uncapped). In the game window: F9 = dump this frame for analysis, F10 = screenshot.
powershell -NoProfile -ExecutionPolicy Bypass -File E:\gtav-native\play.ps1 -Log play -Fps 0 -Counter
