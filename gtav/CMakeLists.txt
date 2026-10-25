# gtav - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gtav LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# The SDK installs its codegen tool for Release only; the Windows SDK is a RelWithDebInfo build.
# (On Linux the SDK is built in Release, so rex::rexglue resolves without this.)
if(WIN32 AND TARGET rex::rexglue)
    # rexglueConfig maps every config to Release, so give Release a location (copied into <prefix>/bin).
    get_filename_component(_rex_prefix "${rexglue_DIR}/../../.." ABSOLUTE)
    set_property(TARGET rex::rexglue APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(rex::rexglue PROPERTIES
        IMPORTED_LOCATION_RELEASE "${_rex_prefix}/bin/rexgluerd.exe")
endif()

# Sources
set(GTAV_SOURCES
    src/main.cpp
    src/crash_handler.cpp
    src/cutscene_audio_fix.cpp
)

if(WIN32)
    add_executable(gtav WIN32 ${GTAV_SOURCES})
else()
    add_executable(gtav ${GTAV_SOURCES})
endif()

rexglue_setup_target(gtav)
