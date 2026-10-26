@echo off
rem Comparison run: the emulated renderer (game window) and the native renderer (second window) side by side.
rem Slow (both renderers draw). Click the "native renderer (shadow)" window and press F9 on a glitch:
rem both renderers dump that frame (build\logs\native_dump and build\logs\emu_dump).
powershell -NoProfile -ExecutionPolicy Bypass -File E:\gtav-native\play.ps1 -Log compare -Fps 30 -Mode shadow -Counter
