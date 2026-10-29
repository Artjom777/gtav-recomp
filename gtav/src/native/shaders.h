// gtav - Xenos shader capture (see shaders.cpp).

#pragma once

#include <cstdint>

namespace gtav::native {

// XenosRecomp cache key of the container a guest D3D shader object was created from, or 0.
uint64_t ShaderHash(uint32_t guest_object);

// Xenia's key for the same shader (XXH3 of its raw microcode), for comparing with the emulator.
uint64_t ShaderUcodeHash(uint32_t guest_object);

// Float constant registers (c0 .. cN-1 of its bank) the shader can read: 256 when it indexes them
// dynamically or is unknown. Draws copy and upload only these.
uint32_t ShaderConstantCount(uint32_t guest_object);

// Container hash of a created shader with this microcode hash, or 0 (diagnostics).
uint64_t ContainerForUcode(uint64_t ucode_hash);

}  // namespace gtav::native
