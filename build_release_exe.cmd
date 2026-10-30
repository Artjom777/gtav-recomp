@echo off
rem Build the release exe (GTAV_GAME_DLL: no recompiled game code inside; it loads gtav_game.dll).
rem Tree: E:\gtav-native\build-dll. Usage: build_release_exe.cmd [jobs]
set JOBS=%1
if "%JOBS%"=="" set JOBS=4
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;%PATH%
cd /d E:\gtav-native\gtav || exit /b 1
echo [%date% %time%] configure
if not exist E:\gtav-native\build-dll\build.ninja (
  cmake --preset win-amd64-relwithdebinfo -B E:\gtav-native\build-dll -DGTAV_GAME_DLL=ON -Drexglue_DIR=E:/gtav-rex/rexglue-sdk/out/install/win-amd64/lib/cmake/rexglue -DREXSDK=E:\gtav-rex\rexglue-sdk\out\install\win-amd64 -DREX_SDK_SRC=E:/gtav-rex/rexglue-sdk/src || (echo CONFIGURE_FAILED & exit /b 4)
)
echo [%date% %time%] build
cmake --build E:\gtav-native\build-dll -j %JOBS% || (echo BUILD_FAILED & exit /b 5)
copy /y E:\gtav-native\build-dll\gtav.exe E:\gtav-native\build-dll\gtav_fast.exe >nul
copy /y E:\gtav-native\build-dll\gtav.pdb E:\gtav-native\build-dll\gtav_fast.pdb >nul
echo [%date% %time%] EXE_DONE
