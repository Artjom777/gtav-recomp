// gtav - Xenos shader capture (see shaders.cpp).

#pragma once

#include <cstdint>

namespace gtav::native {

// XenosRecomp cache key of the container a guest D3D shader object was created from, or 0.
uint64_t ShaderHash(uint32_t guest_object);

// Xenia's key for the same shader (XXH3 of its raw microcode), for comparing with the emulator.
uint64_t ShaderUcodeHash(uint32_t guest_object);

}  // namespace gtav::native
