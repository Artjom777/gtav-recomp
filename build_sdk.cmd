@echo off
rem Rebuild + install the patched rexglue SDK (RelWithDebInfo) that build.cmd links against.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;%PATH%
cmake --build E:\gtav-rex\rexglue-sdk\out\build\win-amd64 --config RelWithDebInfo -j 6 || (echo SDK_BUILD_FAILED & exit /b 5)
cmake --install E:\gtav-rex\rexglue-sdk\out\build\win-amd64 --config RelWithDebInfo || (echo SDK_INSTALL_FAILED & exit /b 6)
echo SDK_DONE
rem The game loads the GPU plugin from its own folder (build.cmd does not copy it).
copy /y E:\gtav-rex\rexglue-sdk\out\install\win-amd64\bin\rexgpu-xenosrd.dll E:\gtav-native\build\ >nul
copy /y E:\gtav-rex\rexglue-sdk\out\install\win-amd64\bin\rexruntimerd.dll E:\gtav-native\build\ >nul
