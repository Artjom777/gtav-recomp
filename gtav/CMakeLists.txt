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
    src/frame_limit.cpp
    src/native/stats.cpp
    src/native/capture.cpp
    src/native/d3d_probe.cpp
    src/native/d3d_hooks.cpp
    src/native/renderer.cpp
    src/native/shaders.cpp
    src/native/backend.cpp
    src/native/textures.cpp
    src/native/shader_compiler.cpp
)

if(WIN32)
    add_executable(gtav WIN32 ${GTAV_SOURCES})
else()
    add_executable(gtav ${GTAV_SOURCES})
    # The SDK's Vulkan command processor finds rex_skip_rendering_swap with dlsym.
    target_link_options(gtav PRIVATE -Wl,--export-dynamic-symbol=rex_skip_rendering_swap)
endif()

rexglue_setup_target(gtav)

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
    DEPENDS ${GTAV_BLIT_HLSL} ${GTAV_RECTLIST_HLSL} ${GTAV_MEMORY_HLSL}
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
