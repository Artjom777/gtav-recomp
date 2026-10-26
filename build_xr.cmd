@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;%PATH%
cd /d E:\gtav-native\thirdparty\XenosRecomp || exit /b 1
if not exist E:\gtav-native\build-xr\build.ninja cmake -G Ninja -B E:\gtav-native\build-xr -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl || (echo CONFIGURE_FAILED & exit /b 4)
cmake --build E:\gtav-native\build-xr -j 6 --target XenosRecomp || (echo BUILD_FAILED & exit /b 5)
echo XR_DONE
