// gtav - diagnostic trace for the prologue vault hang (temporary).
//
// prologue1 stage 6 waits on HAS_CUTSCENE_LOADED for pro_mcs_2. At the hang the cutscene
// manager sits in state 2 with no cut file attached, although the cut file is streamed in.
// Every 2 s this logs how often the cutscene update chain runs and the manager state.

#include <rex/hook.h>
#include <rex/logging.h>

#include <fmt/format.h>
#include <string>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace {

std::atomic<uint8_t*> g_base{nullptr};
std::atomic<uint32_t> c_dispatch{0}, c_update{0}, c_states{0}, c_state2{0};

uint32_t be32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
uint16_t be16(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint16_t(p[0] << 8 | p[1]);
}

constexpr uint32_t kManagerPtr = 0x83A6BE68;  // CutSceneManager*
constexpr uint32_t kCutfileStore = 0x83A6BEF4;
constexpr uint32_t kStreamingInfos = 0x83DE00F4;
constexpr uint32_t kAudioEntity = 0x83A320F8;
constexpr uint32_t kGameTimer = 0x83DDDBE8;
constexpr uint32_t kSkipFlags = 0x83DDDCEC;

// Cut file store slot for a name hash (same lookup as sub_824DC130), or -1.
int CutfileSlot(const uint8_t* base, uint32_t hash) {
  uint16_t mod = be16(base, kCutfileStore + 104);
  if (!mod) return -1;
  uint32_t buckets = be32(base, kCutfileStore + 100), entries = be32(base, kCutfileStore + 96);
  uint16_t i = be16(base, buckets + 2 * (hash % mod));
  while (i != 0xFFFF) {
    uint32_t e = entries + 8 * i;
    if (be32(base, e) == hash) return be16(base, e + 4);
    i = be16(base, e + 6);
  }
  return -1;
}

void Report() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const uint8_t* base = g_base.load();
    uint32_t mgr = be32(base, kManagerPtr);
    if (!mgr) continue;
    uint32_t hash = be32(base, mgr + 3296);
    int slot = CutfileSlot(base, hash);
    uint32_t status = 0, asset = 0;
    if (slot >= 0) {
      uint32_t idx = slot + be32(base, kCutfileStore + 4);
      status = be32(base, be32(base, kStreamingInfos) + 8 * idx + 4);
      uint32_t pool = kCutfileStore + 36;
      asset = be32(base, be32(base, pool) + be32(base, pool + 12) * slot);
    }
    REXLOG_WARN(
        "[cuts] t={} calls dispatch={} update={} states={} state2={} | state={} sub={} cut=0x{:08X} "
        "hash=0x{:08X} slot={} status=0x{:08X} asset=0x{:08X} loaded={} req={} skip={}{}{} "
        "audio_state={}",
        be32(base, kGameTimer), c_dispatch.exchange(0), c_update.exchange(0), c_states.exchange(0),
        c_state2.exchange(0), be32(base, mgr + 3096), be32(base, mgr + 3104), be32(base, mgr + 3160),
        hash, slot, status, asset, base[mgr + 3150], base[mgr + 3415], base[kSkipFlags],
        base[kSkipFlags + 1], base[kSkipFlags + 2], be32(base, kAudioEntity + 5068));
    // Cutscene audio stream slots (controller 0x83A320C0 + 10296): stream ptr, play state byte
    // (+112, &3 == 2 is playing) and the counters that stall short of the end.
    std::string slots;
    for (uint32_t i = 0; i < 2; i++) {
      uint32_t s = be32(base, 0x83A320C0 + 10296 + 4 * i);
      if (!s) {
        slots += fmt::format(" [{}: free]", i);
        continue;
      }
      if (s < 0xC0000000 || s >= 0xE0000000) {  // not a stream yet (early boot): don't follow it
        slots += fmt::format(" [{}: 0x{:08X}?]", i, s);
        continue;
      }
      slots += fmt::format(" [{}: 0x{:08X} st=0x{:02X} {:X} {:X} {:X} {:X} {:X}]", i, s, base[s + 112],
                           be32(base, s + 124), be32(base, s + 128), be32(base, s + 132),
                           be32(base, s + 136), be32(base, s + 140));
    }
    REXLOG_WARN("[cuts] slots audio_idx={}/{} last_slot={}{}", be32(base, kAudioEntity + 5032),
                be32(base, kAudioEntity + 5040), int32_t(be32(base, kAudioEntity + 5048)), slots);
  }
}

void Touch(uint8_t* base) {
  uint8_t* expected = nullptr;
  if (g_base.compare_exchange_strong(expected, base)) std::thread(Report).detach();
}

}  // namespace

// Game-skeleton entry: CutSceneManager::GetInstance()->vtbl[5]().
REX_EXTERN(__imp__sub_824C9930);
REX_HOOK_RAW(sub_824C9930) {
  Touch(base);
  c_dispatch++;
  __imp__sub_824C9930(ctx, base);
}

// CutSceneManager vtbl[5]: skip-flag gate, then the state machine.
REX_EXTERN(__imp__sub_824DEA50);
REX_HOOK_RAW(sub_824DEA50) {
  Touch(base);
  c_update++;
  __imp__sub_824DEA50(ctx, base);
}

// cutsManager state machine (switch on state at +3096).
REX_EXTERN(__imp__sub_831343D8);
REX_HOOK_RAW(sub_831343D8) {
  c_states++;
  __imp__sub_831343D8(ctx, base);
}

// State 2 handler: attach the streamed cut file.
void UnstickCutsceneAudio(PPCContext& ctx, uint8_t* base);
REX_EXTERN(__imp__sub_824DC130);
REX_HOOK_RAW(sub_824DC130) {
  c_state2++;
  UnstickCutsceneAudio(ctx, base);
  __imp__sub_824DC130(ctx, base);
}

// Script WAIT native: runs every frame, so the reporter starts even if the cutscene chain never does.
REX_EXTERN(__imp__sub_83524478);
REX_HOOK_RAW(sub_83524478) {
  Touch(base);
  __imp__sub_83524478(ctx, base);
}

// ---- Cutscene audio release path (why does the audio entity stay in state 5?) ----
namespace {
constexpr uint32_t kAudioObj = 0x83A320F8;  // cutscene audio entity (controller 0x83A320C0 + 56)
constexpr uint32_t kRelFlag0 = 0x83A6BE66, kRelFlag1 = 0x83A6BE67;

std::string MgrFlags(const uint8_t* base) {
  uint32_t mgr = be32(base, kManagerPtr);
  if (!mgr) return "mgr=0";
  return fmt::format("mstate={} cut=0x{:08X} f3147={} f3411={} f3412={} f3415={} g66={} g67={} audio={}",
                     be32(base, mgr + 3096), be32(base, mgr + 3160), base[mgr + 3147],
                     base[mgr + 3411], base[mgr + 3412], base[mgr + 3415], base[kRelFlag0],
                     base[kRelFlag1], be32(base, kAudioObj + 5068));
}
}  // namespace

#define CUTS_LOG_HOOK(addr, label)                                                             \
  REX_EXTERN(__imp__sub_##addr);                                                               \
  REX_HOOK_RAW(sub_##addr) {                                                                   \
    REXLOG_WARN("[cuts] " label " lr=0x{:08X} r3=0x{:08X} r4=0x{:X} r5=0x{:X} r6=0x{:X} | {}", \
                uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,              \
                MgrFlags(base));                                                               \
    __imp__sub_##addr(ctx, base);                                                              \
  }

CUTS_LOG_HOOK(8241BEF0, "audio release")       // stop + release stream slots + Reset
CUTS_LOG_HOOK(823F3068, "audio slot4 release")  // vtbl slot 4
CUTS_LOG_HOOK(82391458, "audio reset")          // vtbl slot 5 -> state 0
CUTS_LOG_HOOK(8243FDF8, "audio stop")           // vtbl slot 3
CUTS_LOG_HOOK(823F2950, "audio start")          // vtbl slot 6 -> state 1
CUTS_LOG_HOOK(824D10B0, "mgr cleanup A")        // release if f3415 && cut && f3412
CUTS_LOG_HOOK(824CD5C8, "mgr cleanup B")        // release if f3415 && !f3147

// Manager event handler: log event ids other than per-frame noise.
REX_EXTERN(__imp__sub_824C9DE0);
REX_HOOK_RAW(sub_824C9DE0) {
  static std::atomic<uint32_t> last_event{~0u};
  uint32_t ev = ctx.r6.u32;
  if (last_event.exchange(ev) != ev)
    REXLOG_WARN("[cuts] event {} r5=0x{:X} | {}", ev, ctx.r5.u32, MgrFlags(base));
  __imp__sub_824C9DE0(ctx, base);
}

// ---- Workaround: cutscene audio streams never report "finished" ----
// At cutscene end the game calls sub_823F30F0(audio, immediate=0, fade). With audio+5072 set,
// that defers the stream release until each stream stops playing on its own, but in our
// runtime a stream that has played to its end (pos +124 >= end +136) stays "playing" forever,
// so the slot is never freed, the audio sits in state 5 and the next cutscene hangs on load
// (the state-2 attach handler waits on IsBusy). While the next cutscene waits, if the audio is
// in state 5 and its streams are at their end, do the immediate release (free the slots with a
// short fade, then Reset) that the game was waiting for.
namespace {
bool StreamAtEnd(const uint8_t* base, int32_t slot_index) {
  if (slot_index < 0) return true;
  uint32_t s = be32(base, 0x83A320C0 + 10296 + 4 * uint32_t(slot_index));
  if (s < 0xC0000000 || s >= 0xE0000000) return true;  // free / not a stream
  uint32_t pos = be32(base, s + 124), end = be32(base, s + 136);
  return end != 0 && pos >= end;
}
}  // namespace

REX_EXTERN(__imp__sub_823F30F0);

// Runs from the state-2 attach handler hook before the original.
void UnstickCutsceneAudio(PPCContext& ctx, uint8_t* base) {
  if (be32(base, kAudioObj + 5068) != 5 || !base[kAudioObj + 5072]) return;
  int32_t a = int32_t(be32(base, kAudioObj + 5044)), b = int32_t(be32(base, kAudioObj + 5048));
  if (!StreamAtEnd(base, a) || !StreamAtEnd(base, b)) return;
  REXLOG_WARN("[cuts] stream(s) {} {} finished but never released: forcing audio release", a, b);
  PPCContext saved = ctx;
  ctx.r3.u64 = kAudioObj;
  ctx.r4.u64 = 1;   // immediate
  ctx.r5.u64 = 10;  // fade ms
  __imp__sub_823F30F0(ctx, base);
  ctx = saved;
}
