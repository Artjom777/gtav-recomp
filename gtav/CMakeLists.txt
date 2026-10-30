# gtav - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gtav LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GTAV_GAME_DLL: the release layout. The exe holds no recompiled game code; it loads gtav_game.dll,
# which the player's installer generates from their own discs and builds with the bundled clang
# (fs/gtav_pch.h). The exe then needs no generated sources at all.
option(GTAV_GAME_DLL "Build gtav_fast.exe against a separately built gtav_game.dll" OFF)

if(GTAV_GAME_DLL)
    find_package(rexglue 0.10.0 REQUIRED CONFIG)
else()
    include(generated/rexglue.cmake)
endif()

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
    src/frame_limit.cpp
    src/wait_profile.cpp
    src/gpu_fence_wait.cpp
    src/native/stats.cpp
    src/native/capture.cpp
    src/native/d3d_probe.cpp
    src/native/d3d_hooks.cpp
    src/native/renderer.cpp
    src/native/shaders.cpp
    src/native/menu.cpp
    src/native/backend.cpp
    src/native/textures.cpp
    src/native/write_watch.cpp
    src/native/shader_compiler.cpp
)

if(WIN32)
    add_executable(gtav WIN32 ${GTAV_SOURCES})
else()
    add_executable(gtav ${GTAV_SOURCES})
    # The SDK's Vulkan command processor finds rex_skip_rendering_swap with dlsym.
    target_link_options(gtav PRIVATE -Wl,--export-dynamic-symbol=rex_skip_rendering_swap
                        -Wl,--export-dynamic-symbol=rex_skip_rendering_fence
                        -Wl,--export-dynamic-symbol=rex_skip_rendering_idle
                        -Wl,--export-dynamic-symbol=rex_skip_rendering_gpu_waiters)
endif()

if(GTAV_GAME_DLL)
    target_sources(gtav PRIVATE src/game_bridge.cpp)
    target_compile_definitions(gtav PRIVATE GTAV_GAME_DLL=1)
    target_include_directories(gtav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gtav PRIVATE rex::runtime)
    rexglue_configure_target(gtav)
    # Pre-link: list the hooks and the game functions the exe calls (names only), build the import
    # library for gtav_game.dll and the export list the DLL links against (gtav_fast.def).
    get_filename_component(GTAV_LLVM_BIN "${CMAKE_LINKER}" DIRECTORY)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(GTAV_INTERFACE_DIR ${CMAKE_BINARY_DIR}/game_interface)
    add_custom_command(TARGET gtav PRE_LINK
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_dll/gen_interface.py
                ${GTAV_LLVM_BIN} ${CMAKE_BINARY_DIR}/CMakeFiles/gtav.dir ${GTAV_INTERFACE_DIR}
                $<TARGET_FILE:rex::runtime>
        VERBATIM)
    target_link_options(gtav PRIVATE "LINKER:/DEF:${GTAV_INTERFACE_DIR}/gtav_fast.def"
                        "LINKER:${GTAV_INTERFACE_DIR}/gtav_game.lib")
else()
    rexglue_setup_target(gtav)
endif()

# The SDK builds recompiled code with -mcmodel=large ("large executable support"): every call
# between the ~100 MB of game functions then goes through a 64-bit address in a register
# (movabs + indirect call), and the game makes calls constantly (__savegprlr/__restgprlr in most
# functions). The code is far below 2 GB, so the default model (direct calls) works. x86-64-v3
# (Haswell / Excavator and newer) adds MOVBE (a big-endian guest load or store is one
# instruction), BMI2 and AVX2. GTAV_X86_64_V3=OFF builds for any x86-64 with SSE4.1.
if(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    option(GTAV_X86_64_V3 "Build the game code for x86-64-v3 CPUs (MOVBE, BMI2, AVX2)" ON)
    foreach(_gtav_target gtav gtav_recomp)
        if(NOT TARGET ${_gtav_target})
            continue()
        endif()
        target_compile_options(${_gtav_target} PRIVATE -mcmodel=small)
        if(GTAV_X86_64_V3)
            target_compile_options(${_gtav_target} PRIVATE -march=x86-64-v3)
        endif()
    endforeach()
elseif(WIN32 AND NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # Windows: the SDK already uses the default code model; only the CPU level is added.
    option(GTAV_X86_64_V3 "Build the game code for x86-64-v3 CPUs (MOVBE, BMI2, AVX2)" ON)
    if(GTAV_X86_64_V3)
        foreach(_gtav_target gtav gtav_recomp)
            if(TARGET ${_gtav_target})
                target_compile_options(${_gtav_target} PRIVATE -march=x86-64-v3)
            endif()
        endforeach()
    endif()
endif()

# The SDK exports REXGLUE_ENABLE_PROFILING / _PERF_COUNTERS to every non-Release build: each
# indirect guest call then does an atomic add on one shared counter in rexruntime (~3% of every
# game thread in a Windows profile, plus cache-line traffic between them). Off for play.
option(GTAV_SDK_COUNTERS "Keep the SDK's guest call counters in the game code" OFF)
if(NOT GTAV_SDK_COUNTERS)
    foreach(_gtav_target gtav gtav_recomp)
        if(TARGET ${_gtav_target})
            target_compile_options(${_gtav_target} PRIVATE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
        endif()
    endforeach()
endif()

# Profile-guided optimization of the recompiled game code (clang): GTAV_PGO=GENERATE builds an
# instrumented game (writes LLVM_PROFILE_FILE when it exits normally or on the profile flush); merge
# the .profraw files with llvm-profdata into GTAV_PGO_PROFILE and build with GTAV_PGO=USE.
set(GTAV_PGO "OFF" CACHE STRING "Profile-guided optimization of the game code: OFF | GENERATE | USE")
set(GTAV_PGO_PROFILE "" CACHE FILEPATH "Merged .profdata for GTAV_PGO=USE")
if(GTAV_PGO STREQUAL "GENERATE")
    target_compile_options(gtav_recomp PRIVATE -fprofile-instr-generate)
    target_link_options(gtav PRIVATE -fprofile-instr-generate)
    target_sources(gtav PRIVATE src/pgo_dump.cpp)
elseif(GTAV_PGO STREQUAL "USE")
    if(NOT EXISTS "${GTAV_PGO_PROFILE}")
        message(FATAL_ERROR "GTAV_PGO=USE needs GTAV_PGO_PROFILE (a merged .profdata)")
    endif()
    target_compile_options(gtav_recomp PRIVATE "-fprofile-instr-use=${GTAV_PGO_PROFILE}"
                           -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
endif()

# Native renderer: Plume (Vulkan, SDL3 surface). SDL3 itself comes from librexruntime, which
# exports it, so only the headers are needed here.
# Windows: rexruntime.dll exports none of its static SDL3, so the native window is Win32 (Plume's
# HWND path) and no SDL is linked here.
if(NOT WIN32)
    find_package(SDL3 REQUIRED CONFIG COMPONENTS Headers)
    set(PLUME_SDL3_VULKAN_ENABLED ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/plume ${CMAKE_BINARY_DIR}/plume EXCLUDE_FROM_ALL)
set_target_properties(plume PROPERTIES POSITION_INDEPENDENT_CODE ON)
option(GTAV_NATIVE_VALIDATION "Enable the Vulkan validation layer on the native renderer's instance" OFF)
if(GTAV_NATIVE_VALIDATION)
    target_compile_definitions(plume PRIVATE VULKAN_VALIDATION_LAYER_ENABLED)
endif()
target_link_libraries(gtav PRIVATE plume)

# The texture decoder uses the SDK's Xenos texture helpers (TextureInfo, tiling), which live in
# the xenos GPU plugin library.
# On Windows the plugin DLL exports only its factory, so those sources are compiled in instead.
if(WIN32)
    set(REX_SDK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/src" CACHE PATH "ReXGlue SDK source tree")
    target_sources(gtav PRIVATE
        ${REX_SDK_SRC}/graphics/pipeline/texture/info.cpp
        ${REX_SDK_SRC}/graphics/pipeline/texture/info_formats.cpp
        ${REX_SDK_SRC}/graphics/pipeline/texture/util.cpp
        ${REX_SDK_SRC}/graphics/xenos.cpp)
    target_compile_definitions(gtav PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(gtav PRIVATE winmm)
    # rex_shader_float_constant_bitmap and rex_skip_rendering_wake are dllexported by the GPU plugin.
    find_library(REXGPU_XENOS NAMES rexgpu-xenosrd rexgpu-xenos PATHS ${rexglue_DIR}/../.. NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(gtav PRIVATE ${REXGPU_XENOS})
else()
    find_library(REXGPU_XENOS rexgpu-xenos PATHS ${rexglue_DIR}/../.. NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(gtav PRIVATE ${REXGPU_XENOS})
endif()

# Native renderer's own shaders (present blit), compiled to SPIR-V headers with XenosRecomp's DXC.
set(GTAV_DXC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/XenosRecomp/thirdparty/dxc-bin)
if(WIN32)
    set(GTAV_DXC ${GTAV_DXC_DIR}/bin/x64/dxc.exe)
else()
    set(GTAV_DXC ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${GTAV_DXC_DIR}/lib/x64 ${GTAV_DXC_DIR}/bin/x64/dxc-linux)
endif()
set(GTAV_NATIVE_SHADER_DIR ${CMAKE_BINARY_DIR}/native_shaders)
set(GTAV_BLIT_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_blit.hlsl)
set(GTAV_RECTLIST_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_rectlist.hlsl)
set(GTAV_MEMORY_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_memory.hlsl)
set(GTAV_MENU_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_menu.hlsl)
add_custom_command(
    OUTPUT ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_NATIVE_SHADER_DIR}
    COMMAND ${GTAV_DXC}
            -T vs_6_0 -E VSMain -spirv -fspv-target-env=vulkan1.1 -Vn g_native_blit_vs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_blit_vs.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSMain -spirv -fspv-target-env=vulkan1.1 -Vn g_native_blit_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_blit_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDepth -spirv -fspv-target-env=vulkan1.1 -Vn g_native_depth_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_depth_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSResolve -spirv -fspv-target-env=vulkan1.1 -Vn g_native_resolve_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_resolve_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDepthPack -spirv -fspv-target-env=vulkan1.1 -Vn g_native_depth_pack_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_depth_pack_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T gs_6_0 -E main -spirv -fspv-target-env=vulkan1.1 -Vn g_native_rectlist_gs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_rectlist_gs.h ${GTAV_RECTLIST_HLSL}
    COMMAND ${GTAV_DXC}
            -T cs_6_0 -E CSResolveToMemory -spirv -fspv-target-env=vulkan1.1 -Vn g_native_resolve_memory_cs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_resolve_memory_cs.h ${GTAV_MEMORY_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDigits -spirv -fspv-target-env=vulkan1.1 -Vn g_native_digits_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_digits_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSAlpha -spirv -fspv-target-env=vulkan1.1 -Vn g_native_alpha_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_alpha_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T vs_6_0 -E VSMenu -HV 2021 -spirv -Vn g_native_menu_vs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_menu_vs.h ${GTAV_MENU_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSMenu -HV 2021 -spirv -Vn g_native_menu_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_menu_ps.h ${GTAV_MENU_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDiagFlat -spirv -fspv-target-env=vulkan1.1 -Vn g_native_diag_flat_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_diag_flat_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" > ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_digits_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_diag_flat_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_alpha_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_resolve_memory_cs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_rectlist_gs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_depth_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_resolve_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_depth_pack_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_blit_vs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_blit_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_menu_vs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_menu_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    DEPENDS ${GTAV_BLIT_HLSL} ${GTAV_RECTLIST_HLSL} ${GTAV_MEMORY_HLSL} ${GTAV_MENU_HLSL}
    VERBATIM)
add_custom_target(gtav_native_shaders DEPENDS ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h)
add_dependencies(gtav gtav_native_shaders)
target_include_directories(gtav PRIVATE ${GTAV_NATIVE_SHADER_DIR})

# Background shader compiler tools (shader_compiler.cpp; build.cmd copies them on Windows):
# <exe>/tools/XenosRecomp, dxc (a wrapper that sets the library path) and shader_common.h.
if(NOT WIN32)
    set(GTAV_XR_BIN ${CMAKE_CURRENT_SOURCE_DIR}/../build/xenosrecomp/XenosRecomp/XenosRecomp CACHE FILEPATH
        "XenosRecomp binary for the background shader compiler")
    set(GTAV_TOOLS_DIR $<TARGET_FILE_DIR:gtav>/tools)
    file(WRITE ${CMAKE_BINARY_DIR}/dxc_wrapper.sh
         "#!/bin/sh\nd=$(dirname \"$0\")\nLD_LIBRARY_PATH=\"$d/lib\" exec \"$d/dxc-linux\" \"$@\"\n")
    add_custom_command(TARGET gtav POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_TOOLS_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTAV_XR_BIN} ${GTAV_TOOLS_DIR}/XenosRecomp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTAV_DXC_DIR}/bin/x64/dxc-linux ${GTAV_TOOLS_DIR}/dxc-linux
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${GTAV_DXC_DIR}/lib/x64 ${GTAV_TOOLS_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/dxc_wrapper.sh ${GTAV_TOOLS_DIR}/dxc
        COMMAND chmod +x ${GTAV_TOOLS_DIR}/dxc
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/XenosRecomp/XenosRecomp/shader_common.h ${GTAV_TOOLS_DIR}/shader_common.h
        VERBATIM)
endif()
