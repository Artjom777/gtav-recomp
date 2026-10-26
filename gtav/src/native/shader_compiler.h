// gtav - background compiler for shaders without precompiled SPIR-V (see shader_compiler.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace gtav::native {

// Called for every shader the game creates; queues the ones without SPIR-V.
void OnShaderCreated(const uint8_t* container, size_t size, uint64_t hash, bool pixel);

// Bumped whenever a new SPIR-V file landed: cached misses should be looked up again.
uint64_t CompileGeneration();

}  // namespace gtav::native
