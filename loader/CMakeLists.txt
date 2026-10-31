# gtav launcher: first-run setup (the player's own discs + TU27 -> data\ and gtav_game.dll) and
# game launcher. Static C runtime, so it starts on a PC without the Visual C++ runtime and can
# tell the player to install it.
# macOS: GTAV.app with an AppKit setup window (src/main_mac.mm) that builds gtav_game.dylib.
# Linux: a setup window (GTK 3, dlopen'ed) or the terminal (src/main_linux.cpp) that builds
# gtav_game.so; static libstdc++ and libgcc, so it needs only glibc.
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
elseif(APPLE)
    # GTAV.app: the launcher is the bundle's executable; the package (gtav_fast, dylibs, rexglue,
    # tools/) goes to Contents/Resources/gtav (tools/release/make_release_macos.py).
    enable_language(OBJCXX)
    add_executable(gtav_launcher MACOSX_BUNDLE
        src/main_mac.mm
        src/install.cpp
        src/sources.cpp)
    set_target_properties(gtav_launcher PROPERTIES
        OUTPUT_NAME GTAV
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
    target_compile_options(gtav_launcher PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(gtav_launcher PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
else()
    add_executable(gtav_launcher
        src/main_linux.cpp
        src/install.cpp
        src/sources.cpp)
    target_link_options(gtav_launcher PRIVATE -static-libstdc++ -static-libgcc)
    target_link_libraries(gtav_launcher PRIVATE ${CMAKE_DL_LIBS})
    # The setup window: GTK 3 headers only (types and enums); the library is dlopen'ed at run time,
    # so the launcher still starts without GTK (terminal setup).
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 QUIET gtk+-3.0)
    endif()
    if(GTK3_FOUND)
        target_include_directories(gtav_launcher SYSTEM PRIVATE ${GTK3_INCLUDE_DIRS})
        target_compile_definitions(gtav_launcher PRIVATE GTAV_GTK_UI)
    else()
        message(WARNING "gtk+-3.0 headers not found: the Linux launcher is built without its setup window")
    endif()
endif()
