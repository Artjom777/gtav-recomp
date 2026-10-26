// gtav - Xenos shader capture: container -> hash, and guest shader object -> hash.
//
// The XDK shader create functions take a shader container (flags 0x102A11xx, then virtual and
// physical sizes) in r3 and return the new D3D shader object in r3. Each container is hashed
// the way XenosRecomp keys its shader cache (XXH3-64 over virtualSize + physicalSize bytes),
// so a guest shader object bound at draw time maps to its precompiled host shader.
// --gtav_native_shader_dump=DIR also writes every container to DIR/<hash>.bin, which is the
// input for the offline XenosRecomp pass.

#include "shaders.h"
#include "shader_compiler.h"

#include <rex/cvar.h>
#include <rex/kernel.h>
#include <rex/system.h>
#include <rex/hook.h>
#include <rex/logging.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <atomic>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

REXCVAR_DEFINE_STRING(gtav_native_shader_dump, "", "GTAV",
                      "Directory to dump every Xenos shader container the game creates");

namespace gtav::native {
namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

std::mutex g_mutex;
std::unordered_map<uint32_t, uint64_t> g_object_hash;  // guest shader object -> container hash
std::unordered_map<uint32_t, uint64_t> g_object_ucode_hash;  // -> microcode hash (Xenia's key)
std::atomic<uint32_t> g_created{0}, g_dumped{0};

void OnCreate(const uint8_t* base, uint32_t container, uint32_t object, bool pixel) {
  if (!object || !container) return;
  const uint8_t* c = REX_KERNEL_MEMORY()->TranslateVirtual(container);
  uint32_t flags = be32(c), size = be32(c + 4) + be32(c + 8);
  if ((flags & 0xFFFFFF00) != 0x102A1100 || size > (16u << 20)) return;
  uint64_t hash = XXH3_64bits(c, size);
  // Xenia keys shaders by XXH3 of the raw (big-endian) microcode: the container's physical part,
  // located by the shader header (physical offset, size) at container + shaderOffset.
  uint32_t header = be32(c + 0x18);
  uint32_t ucode_offset = be32(c + 4) + be32(c + header), ucode_size = be32(c + header + 4);
  uint64_t ucode_hash = ucode_offset + ucode_size <= size ? XXH3_64bits(c + ucode_offset, ucode_size) : 0;
  {
    std::lock_guard lock(g_mutex);
    g_object_hash[object] = hash;
    g_object_ucode_hash[object] = ucode_hash;
  }
  OnShaderCreated(c, size, hash, pixel);
  uint32_t created = g_created.fetch_add(1, std::memory_order_relaxed);
  if (created < 4) {
    // Layout discovery: which object field points at the (physical) microcode copy?
    const uint8_t* phys = c + be32(c + 4);
    auto* mem = REX_KERNEL_MEMORY();
    const uint8_t* obj = mem->TranslateVirtual(object);
    for (uint32_t off = 0; off < 872; off += 4) {
      uint32_t v = be32(obj + off);
      if (v < 0x1000) continue;
      const uint8_t* cand = (v >= 0x80000000) ? mem->TranslateVirtual(v) : mem->TranslatePhysical(v);
      if (v < 0x20000000 || v >= 0xA0000000) {
        if (std::memcmp(cand, phys, 32) == 0)
          REXLOG_INFO("[native-probe] {} obj {:08X}: microcode ptr at +{} = {:08X} (phys size {})",
                      pixel ? "ps" : "vs", object, off, v, be32(c + 8));
      }
    }
  }
  const std::string& dir = REXCVAR_GET(gtav_native_shader_dump);
  if (dir.empty()) return;
  auto path = std::filesystem::path(dir) / fmt::format("{}_{:016X}.bin", pixel ? "ps" : "vs", hash);
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return;
  std::filesystem::create_directories(dir, ec);
  if (FILE* f = std::fopen(path.string().c_str(), "wb")) {
    std::fwrite(c, 1, size, f);
    std::fclose(f);
    uint32_t n = g_dumped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n % 250 == 0) REXLOG_INFO("[native] dumped {} shader containers", n);
  }
}

}  // namespace

uint64_t ShaderHash(uint32_t guest_object) {
  std::lock_guard lock(g_mutex);
  auto it = g_object_hash.find(guest_object);
  return it == g_object_hash.end() ? 0 : it->second;
}

uint64_t ShaderUcodeHash(uint32_t guest_object) {
  std::lock_guard lock(g_mutex);
  auto it = g_object_ucode_hash.find(guest_object);
  return it == g_object_ucode_hash.end() ? 0 : it->second;
}

}  // namespace gtav::native

// Shader create functions (which one is vertex vs pixel is confirmed from the dumped
// containers; the guess follows the call order in RAGE's program loader).
REX_EXTERN(__imp__sub_83813218);
REX_HOOK_RAW(sub_83813218) {
  uint32_t container = ctx.r3.u32;
  __imp__sub_83813218(ctx, base);
  gtav::native::OnCreate(base, container, ctx.r3.u32, false);
}

REX_EXTERN(__imp__sub_83813030);
REX_HOOK_RAW(sub_83813030) {
  uint32_t container = ctx.r3.u32;
  __imp__sub_83813030(ctx, base);
  gtav::native::OnCreate(base, container, ctx.r3.u32, true);
}
