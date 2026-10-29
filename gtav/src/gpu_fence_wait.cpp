// gtav - the game blocking on a GPU fence (native renderer, --gpu_skip_rendering).
//
// Every frame the update thread waits until the GPU has finished an earlier frame (sub_835833D8
// -> sub_832BA460 -> D3D 8380CEF8, which spins on the completed-fence value at device+11024).
// The command processor runs --gpu_skip_rendering_lag_ms behind the CPU's kicks (so draw data
// the game fills after a kick is complete when it is read), which made every such wait take the
// whole lag: ~6 ms of a ~20 ms frame. While a game thread is blocked here the SDK command
// processor lets kicks through one at a time instead (rex_skip_rendering_gpu_waiters).

#include <rex/hook.h>

#include <atomic>

#include "wait_profile.h"

namespace {
std::atomic<int> g_gpu_waiters{0};
}

#ifdef _WIN32
#define GTAV_HOOK_EXPORT __declspec(dllexport)
#else
#define GTAV_HOOK_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
extern "C" GTAV_HOOK_EXPORT int rex_skip_rendering_gpu_waiters() { return g_gpu_waiters.load(std::memory_order_acquire); }

// D3D: block until the GPU passes a fence.
REX_EXTERN(__imp__sub_8380CEF8);
REX_HOOK_RAW(sub_8380CEF8) {
  g_gpu_waiters.fetch_add(1, std::memory_order_acq_rel);
  gtav::WaitProfileTimed(0x8380CEF8, ctx, base, [](PPCContext& c, uint8_t* b) { __imp__sub_8380CEF8(c, b); });
  g_gpu_waiters.fetch_sub(1, std::memory_order_acq_rel);
}
