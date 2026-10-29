// gtav - frame-rate limit (--gtav_fps_limit).
//
// The game paces itself in sub_827B5878: while less than N/60 s (N = the int at 0x8391D9D4,
// 2 on the 360) passed since the last frame (timebase), it sleeps. N <= 0 skips the wait.
// --gtav_fps_limit=30 (default) leaves the game's limiter alone; any other value sets N = 0
// and, unless 0 (uncapped), paces frames here to that rate instead. The game itself is not
// the only limit: with the emulated GPU's vblank at 60 Hz (--vsync=true) other waits still
// tie it to the display, so uncapped play also wants --vsync=false.

#include <rex/cvar.h>
#include <rex/hook.h>

#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

REXCVAR_DEFINE_INT32(gtav_fps_limit, 30, "GTAV",
                     "Frame-rate limit: 30 = the game's own limiter, 0 = uncapped, other = that rate");
REXCVAR_DEFINE_BOOL(gtav_audio_unsync, true, "GTAV",
                    "Above 30 fps: stop the audio update waiting for the next 16 ms audio frame "
                    "(otherwise the game cannot exceed 62.5 fps)");
REXCVAR_DEFINE_BOOL(gtav_audio_next_pass, true, "GTAV",
                    "Above 30 fps: the audio engine runs a game thread's sound commands at its next "
                    "5.3 ms pass instead of three passes later (the update thread waits for that every frame)");

namespace {

constexpr uint32_t kFrameIntervalVblanks = 0x8391D9D4;  // N: frame time in 1/60 s units
// The audio update (sub_831D8A68, run by the thread in sub_82488170 that the main loop waits on
// every frame) polls up to 1 s for the audio engine's next frame (768 samples at 48 kHz = 16 ms);
// the main loop therefore never exceeds 62.5 fps. When that poll times out the game sets this
// byte and stops waiting from then on: its own no-audio-sync fallback, set here instead.
constexpr uint32_t kAudioSyncTimedOut = 0x83D0D4CD;

void Store32(uint8_t* base, uint32_t addr, int32_t value) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(uint32_t(value) >> 24), p[1] = uint8_t(value >> 16), p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

#ifdef _WIN32
// Windows rounds every host sleep (this limiter's and the guest's) up to the 15.6 ms system tick
// unless the process asks for 1 ms; at 60 fps that alone costs whole frames.
const bool g_timer_resolution = [] { return timeBeginPeriod(1) == TIMERR_NOERROR; }();
#endif

}  // namespace

// The audio engine (XAudio2 processing passes, 256 samples = 5.3 ms) keeps a command buffer per
// game thread: sub_831B83A0(engine, stamp) marks the calling thread's buffer submitted at audio
// pass `stamp`, and the pass handler (sub_831CD850) runs it once the pass counter reaches
// stamp + 3, then releases that thread's semaphore. The update thread waits on it at the start
// of every frame (sub_8248EA58 -> sub_831B83F0): 16 ms after its submission, so ~60 fps at most
// (a quarter of the update thread's time at 14 ms frames). Stamped two passes earlier, the next
// pass runs it.
REX_EXTERN(__imp__sub_831B83A0);
REX_HOOK_RAW(sub_831B83A0) {
  if (REXCVAR_GET(gtav_fps_limit) != 30 && REXCVAR_GET(gtav_audio_next_pass) && ctx.r4.u32 >= 2)
    ctx.r4.u64 = ctx.r4.u32 - 2;
  __imp__sub_831B83A0(ctx, base);
}

REX_EXTERN(__imp__sub_827B5878);
REX_HOOK_RAW(sub_827B5878) {
  int32_t limit = REXCVAR_GET(gtav_fps_limit);
  if (limit == 30) {
    __imp__sub_827B5878(ctx, base);
    return;
  }
  Store32(base, kFrameIntervalVblanks, 0);
  if (REXCVAR_GET(gtav_audio_unsync)) base[kAudioSyncTimedOut] = 1;
  __imp__sub_827B5878(ctx, base);
  if (limit <= 0) return;
  using clock = std::chrono::steady_clock;
  static clock::time_point next = clock::now();
  auto period = std::chrono::nanoseconds(1000000000 / limit);
  auto now = clock::now();
  if (next > now) std::this_thread::sleep_until(next);
  next = std::max(next + period, now);  // no catch-up after a long frame
}
