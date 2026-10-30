// gtav - runtime interface for the game DLL (GTAV_GAME_DLL build, see fs/gtav_pch.h).
//
// The recompiled game code is built on the player's machine without the SDK headers, so the
// handful of runtime services the SDK's generated prefix header calls inline are exported here as
// plain C functions. Each one does what the matching macro in the SDK's gtav_pch.h does.

#include <rex/chrono/clock.h>
#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/mmio_handler.h>
#include <rex/thread/mutex.h>

#include <atomic>
#include <cstdlib>
#include <string_view>

#include "../fs/layout_check.h"

namespace {
std::atomic<int32_t> g_global_lock_count{0};
}

extern "C" {

__declspec(dllexport) uint32_t gtavfs_mmio_load(uint32_t addr) {
  uint32_t value = 0;
  rex::runtime::MMIOHandler::global_handler()->CheckLoad(addr, &value);
  return value;
}

__declspec(dllexport) void gtavfs_mmio_store(uint32_t addr, uint32_t value) {
  rex::runtime::MMIOHandler::global_handler()->CheckStore(addr, value);
}

__declspec(dllexport) uint64_t gtavfs_query_timebase() {
  return rex::chrono::Clock::QueryGuestTickCount();
}

__declspec(dllexport) PPCFunc* gtavfs_resolve_indirect(uint32_t guest_address) {
  return rex::runtime::ResolveIndirectFunction(guest_address);
}

__declspec(dllexport) void gtavfs_global_lock_enter() {
  rex::thread::global_critical_region::mutex().lock();
  g_global_lock_count.fetch_add(1);
}

__declspec(dllexport) void gtavfs_global_lock_leave() {
  g_global_lock_count.fetch_sub(1);
  rex::thread::global_critical_region::mutex().unlock();
}

__declspec(dllexport) uint64_t gtavfs_global_lock_check() {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  return g_global_lock_count.load() ? 0 : 0x8000;
}

__declspec(dllexport) void gtavfs_trap(PPCContext* ctx, uint8_t* base, uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26: {
      std::string_view str(reinterpret_cast<const char*>(base + ctx->r3.u32), ctx->r4.u16);
      REXCPU_DEBUG("(service trap) {}", str);
      break;
    }
    case 0:
    case 22:
      REXCPU_WARN("tw/td trap hit (type {})", trap_type);
      break;
    case 25:
      break;
    default:
      REXCPU_WARN("Unknown trap type {}", trap_type);
      break;
  }
}

[[noreturn]] __declspec(dllexport) void gtavfs_unimplemented(const char* what) {
  REXCPU_ERROR("{}", what);
  std::abort();
}

}  // extern "C"
