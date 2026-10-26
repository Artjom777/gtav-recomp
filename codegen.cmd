@echo off
set PATH=E:\gtav-rex\rexglue-sdk\out\win-amd64\RelWithDebInfo;%PATH%
cd /d E:\gtav-native\gtav || exit /b 1
echo [%date% %time%] codegen
rexgluerd.exe --force --log-level info codegen gtav_manifest.toml
echo CODEGEN_EXIT=%errorlevel%
echo [%date% %time%] end
