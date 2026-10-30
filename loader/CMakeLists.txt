# gtav launcher: first-run setup (the player's own discs + TU27 -> data\ and gtav_game.dll) and
# game launcher. Static C runtime, so it starts on a PC without the Visual C++ runtime and can
# tell the player to install it.
# Linux: a terminal launcher (src/main_linux.cpp) that builds gtav_game.so; static libstdc++ and
# libgcc, so it needs only glibc.
cmake_minimum_required(VERSION 3.25)
project(gtav_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_executable(gtav_launcher WIN32
        src/main.cpp
        src/install.cpp
        src/sources.cpp
        src/launcher.manifest)  # visual styles, DPI awareness, long paths
    target_compile_definitions(gtav_launcher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(gtav_launcher PRIVATE comctl32 ole32 shell32 bcrypt)
else()
    add_executable(gtav_launcher
        src/main_linux.cpp
        src/install.cpp
        src/sources.cpp)
    target_link_options(gtav_launcher PRIVATE -static-libstdc++ -static-libgcc)
    target_link_libraries(gtav_launcher PRIVATE ${CMAKE_DL_LIBS})
endif()
