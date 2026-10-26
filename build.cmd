@echo off
rem Configure + build gtav with the native renderer. Tree: E:\gtav-native\build. Usage: build.cmd [jobs]
set JOBS=%1
if "%JOBS%"=="" set JOBS=4
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;%PATH%
cd /d E:\gtav-native\gtav || exit /b 1
echo [%date% %time%] configure
if not exist E:\gtav-native\build\build.ninja (
  cmake --preset win-amd64-relwithdebinfo -B E:\gtav-native\build -DREXSDK=E:\gtav-rex\rexglue-sdk\out\install\win-amd64 -DREX_SDK_SRC=E:/gtav-rex/rexglue-sdk/src || (echo CONFIGURE_FAILED & exit /b 4)
)
echo [%date% %time%] build
cmake --build E:\gtav-native\build -j %JOBS% || (echo BUILD_FAILED & exit /b 5)
copy /y E:\gtav-native\build\gtav.exe E:\gtav-native\build\gtav_fast.exe >nul
copy /y E:\gtav-native\build\gtav.pdb E:\gtav-native\build\gtav_fast.pdb >nul
rem Background shader compiler tools (shader_compiler.cpp)
if not exist E:\gtav-native\build\tools mkdir E:\gtav-native\build\tools
copy /y E:\gtav-native\build-xr\XenosRecomp\XenosRecomp.exe E:\gtav-native\build\tools\ >nul
copy /y E:\gtav-native\thirdparty\XenosRecomp\thirdparty\dxc-bin\bin\x64\*.* E:\gtav-native\build\tools\ >nul
copy /y E:\gtav-native\thirdparty\XenosRecomp\XenosRecomp\shader_common.h E:\gtav-native\build\tools\ >nul
echo [%date% %time%] GAME_DONE
