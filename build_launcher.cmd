@echo off
rem Build the launcher (loader\). Tree: E:\gtav-native\build-launcher.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\gtav-native\loader || exit /b 1
if not exist E:\gtav-native\build-launcher\build.ninja (
  cmake -G Ninja -B E:\gtav-native\build-launcher -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl -DGTAV_SDK_THIRDPARTY=E:/gtav-rex/rexglue-sdk/thirdparty || (echo CONFIGURE_FAILED & exit /b 4)
)
cmake --build E:\gtav-native\build-launcher || (echo BUILD_FAILED & exit /b 5)
echo LAUNCHER_DONE
