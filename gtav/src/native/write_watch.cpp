// gtav - guest physical page write tracking (see write_watch.h).

#include "write_watch.h"

#include <rex/cvar.h>
#include <rex/kernel.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

REXCVAR_DEFINE_BOOL(gtav_native_write_watch, true, "GTAV",
                    "Native renderer: let the kernel track guest writes to cached textures and vertex/index "
                    "buffers instead of hashing them every frame");

namespace gtav::native::write_watch {
namespace {

constexpr uint32_t kPageShift = 12;
constexpr uint32_t kPhysicalSize = 0x20000000;  // 512 MB
constexpr uint32_t kPages = kPhysicalSize >> kPageShift;

std::unique_ptr<std::atomic<uint64_t>[]> g_page_epoch;  // harvest that saw the page written
// Pages a cached copy was made from: only these are kept write-protected (the game writes ~4,000
// other pages a frame; protecting them all cost its update thread ~1 ms of faults a frame).
std::unique_ptr<uint64_t[]> g_interest;
bool Interesting(uint32_t p) { return (g_interest[p >> 6] >> (p & 63)) & 1; }
std::atomic<uint64_t> g_epoch{1};
std::once_flag g_init;
bool g_ready = false;
uint64_t g_harvests = 0, g_found = 0;
std::atomic<uint64_t> g_pages{0};
double g_ms = 0;

void Mark(uint32_t physical, uint64_t bytes, uint64_t epoch) {
  if (physical >= kPhysicalSize) return;
  uint32_t first = physical >> kPageShift;
  uint32_t last = uint32_t(std::min<uint64_t>((uint64_t(physical) + std::max<uint64_t>(bytes, 1) - 1) >> kPageShift,
                                              kPages - 1));
  for (uint32_t p = first; p <= last; p++) g_page_epoch[p].store(epoch, std::memory_order_release);
}

#ifdef __linux__
// The guest views that alias physical memory (SDK map_info): 64 KB, 16 MB and 4 KB page views of
// all 512 MB, and the GPU writeback window over the first 16 MB.
struct View {
  uint32_t guest, size;
  uint8_t* host;
  uint32_t physical;  // physical address of the view's first byte
};
View g_views[4];
uint32_t g_view_count = 0;
int g_pagemap = -1, g_uffd = -1;

void Init() {
  auto* mem = REX_KERNEL_MEMORY();
  if (!mem) return;
  int uffd = int(syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY));
  if (uffd < 0) {
    REXLOG_INFO("[native] write watch off: userfaultfd unavailable ({})", std::strerror(errno));
    return;
  }
  uffdio_api api{};
  api.api = UFFD_API;
  api.features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_HUGETLBFS_SHMEM | UFFD_FEATURE_WP_UNPOPULATED;
  if (ioctl(uffd, UFFDIO_API, &api) != 0) {
    REXLOG_INFO("[native] write watch off: async write-protect unsupported ({})", std::strerror(errno));
    close(uffd);
    return;
  }
  // The 7F window is part of the SDK's virtual 64 KB heap (GetPhysicalAddress knows nothing of
  // it) but maps the first 16 MB of physical memory (SDK map_info, file offset 0).
  static constexpr View kViews[] = {{0xA0000000, 0x20000000, nullptr, ~0u},
                                    {0xC0000000, 0x20000000, nullptr, ~0u},
                                    {0xE0000000, 0x20000000, nullptr, ~0u},
                                    {0x7F000000, 0x01000000, nullptr, 0}};
  for (const View& v : kViews) {
    uint8_t* host = mem->virtual_membase() + v.guest;
    uffdio_register r{};
    r.range.start = reinterpret_cast<uintptr_t>(host);
    r.range.len = v.size;
    r.mode = UFFDIO_REGISTER_MODE_WP;
    if (ioctl(uffd, UFFDIO_REGISTER, &r) != 0) {
      // A view that cannot be watched: its writes would go unseen, so nothing is trusted.
      REXLOG_INFO("[native] write watch off: cannot register view {:08X} ({})", v.guest, std::strerror(errno));
      close(uffd);
      return;
    }
    // From the SDK's own host -> guest -> physical mapping, 1 MB into the view: the 4 KB page
    // view's heap has a 0x1000 host offset (its host base is physical 0, not 0x1000).
    uint32_t physical = v.physical;
    if (physical == ~0u) {
      constexpr uint32_t kProbe = 1 << 20;
      physical = mem->GetPhysicalAddress(mem->HostToGuestVirtual(host + kProbe)) - kProbe;
    }
    uint32_t size = std::min<uint32_t>(v.size, kPhysicalSize - std::min(physical, kPhysicalSize));
    g_views[g_view_count++] = {v.guest, size, host, physical};
    REXLOG_INFO("[native] write watch view {:08X}: physical {:08X}, {} MB", v.guest, physical, size >> 20);
  }
  g_pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
  if (g_pagemap < 0) {
    REXLOG_INFO("[native] write watch off: no /proc/self/pagemap ({})", std::strerror(errno));
    return;
  }
  // The userfaultfd stays open for the process lifetime: closing it would drop the registrations.
  g_uffd = uffd;
  g_interest = std::make_unique<uint64_t[]>(kPages / 64);
  std::memset(g_interest.get(), 0, kPages / 8);
  g_page_epoch = std::make_unique<std::atomic<uint64_t>[]>(kPages);
  for (uint32_t p = 0; p < kPages; p++) g_page_epoch[p].store(0, std::memory_order_relaxed);
  g_ready = true;
  REXLOG_INFO("[native] write watch on: the kernel reports guest writes (userfaultfd async write-protect)");
}

// Harvest (`protect`): marks the pages of [start, end) of a view written since the last harvest
// with `epoch` and protects them again in the same call. Otherwise only counts them (Pending).
bool ScanView(const View& v, uint64_t start, uint64_t end, bool protect, uint64_t epoch) {
  thread_local page_region regions[4096];
  auto* mem = REX_KERNEL_MEMORY();
  while (start < end) {
    pm_scan_arg a{};
    a.size = sizeof(a);
    a.flags = protect ? PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC : 0;  // protect: harvest
    a.start = start;
    a.end = end;
    a.vec = reinterpret_cast<uintptr_t>(regions);
    a.vec_len = std::size(regions);
    a.category_mask = PAGE_IS_WRITTEN;
    a.return_mask = PAGE_IS_WRITTEN;
    long n = ioctl(g_pagemap, PAGEMAP_SCAN, &a);
    if (n < 0) {
      static int logged = 0;
      if (logged++ < 5) REXLOG_INFO("[native] write watch: PAGEMAP_SCAN failed ({})", std::strerror(errno));
      return false;
    }
    for (long i = 0; i < n; i++) {
      uint64_t bytes = regions[i].end - regions[i].start;
      uint32_t physical = v.physical + uint32_t(regions[i].start - reinterpret_cast<uintptr_t>(v.host));
      if (protect) {
        uint32_t first = physical >> kPageShift;
        uint32_t last = uint32_t(std::min<uint64_t>((uint64_t(physical) + bytes - 1) >> kPageShift, kPages - 1));
        for (uint32_t p = first; p <= last; p++)
          if (Interesting(p)) g_page_epoch[p].store(epoch, std::memory_order_release), g_pages++;
      } else {
        g_found += bytes >> kPageShift;  // Pending(): written since the last harvest
      }
    }
    if (a.walk_end <= start) break;
    start = a.walk_end;
  }
  return true;
}
#else
void Init() {}
#endif

}  // namespace

bool Enabled() {
  if (!REXCVAR_GET(gtav_native_write_watch)) return false;
  std::call_once(g_init, Init);
  return g_ready;
}

#ifdef __linux__
namespace {
// Unprotected pages of a view (written, or never protected): the runs of them copies were made
// from are marked and protected again, each run in one call (PAGEMAP_SCAN reports what it
// protects, so a write landing between the two scans is marked too).
bool HarvestSpan(const View& v, uint64_t start, uint64_t end, uint64_t epoch) {
  thread_local page_region regions[4096];
  uint32_t view_physical = v.physical;
  uint64_t base = reinterpret_cast<uintptr_t>(v.host);
  while (start < end) {
    pm_scan_arg a{};
    a.size = sizeof(a);
    a.start = start;
    a.end = end;
    a.vec = reinterpret_cast<uintptr_t>(regions);
    a.vec_len = std::size(regions);
    a.category_mask = PAGE_IS_WRITTEN;
    a.return_mask = PAGE_IS_WRITTEN;
    long n = ioctl(g_pagemap, PAGEMAP_SCAN, &a);
    if (n < 0) return false;
    uint64_t walk_end = a.walk_end;
    // Copy the regions out: ScanView below reuses its own buffer.
    thread_local page_region found[4096];
    std::copy(regions, regions + n, found);
    for (long i = 0; i < n; i++) {
      uint32_t first = uint32_t(view_physical + ((found[i].start - base) >> 0)) >> kPageShift;
      uint32_t last = uint32_t(std::min<uint64_t>((view_physical + (found[i].end - base) - 1) >> kPageShift, kPages - 1));
      for (uint32_t p = first; p <= last;) {
        if (!(g_interest[p >> 6] >> (p & 63))) {  // skip uninteresting pages, a word at a time
          p = (p | 63) + 1;
          continue;
        }
        if (!Interesting(p)) {
          p++;
          continue;
        }
        uint32_t run = p;
        while (p <= last && Interesting(p)) p++;
        uint64_t lo = base + (uint64_t(run) << kPageShift) - view_physical;
        uint64_t hi = base + (uint64_t(p) << kPageShift) - view_physical;
        if (!ScanView(v, lo, hi, true, epoch)) Mark(run << kPageShift, uint64_t(p - run) << kPageShift, epoch);
      }
    }
    if (walk_end <= start) break;
    start = walk_end;
  }
  return true;
}
// Only the 2 MB blocks holding pages copies were made from are scanned (a whole view is
// ~390,000 page table entries, ~1.5 ms a frame for all four).
bool HarvestView(const View& v, uint64_t epoch) {
  constexpr uint32_t kBlockPages = 512, kBlockWords = kBlockPages / 64;
  uint64_t base = reinterpret_cast<uintptr_t>(v.host);
  uint32_t first_block = v.physical / (kBlockPages << kPageShift);
  uint32_t last_block = uint32_t((uint64_t(v.physical) + v.size - 1) / (kBlockPages << kPageShift));
  auto any = [](uint32_t block) {
    for (uint32_t w = 0; w < kBlockWords; w++)
      if (g_interest[block * kBlockWords + w]) return true;
    return false;
  };
  for (uint32_t b = first_block; b <= last_block;) {
    if (!any(b)) {
      b++;
      continue;
    }
    uint32_t span = b;
    while (b <= last_block && any(b)) b++;
    uint64_t lo = std::max<uint64_t>(uint64_t(span) * kBlockPages << kPageShift, v.physical);
    uint64_t hi = std::min<uint64_t>(uint64_t(b) * kBlockPages << kPageShift, uint64_t(v.physical) + v.size);
    if (!HarvestSpan(v, base + (lo - v.physical), base + (hi - v.physical), epoch)) return false;
  }
  return true;
}
}  // namespace
#endif

#ifdef __linux__
namespace {
void HarvestOne(uint32_t i, uint64_t epoch) {
  const View& v = g_views[i];
  if (!HarvestView(v, epoch)) {  // unknown: everything counts as written
    static std::atomic<int> logged{0};
    if (logged++ < 5) REXLOG_INFO("[native] write watch: PAGEMAP_SCAN failed ({})", std::strerror(errno));
    Mark(v.physical, v.size, epoch);
  }
}

// The views are scanned in parallel (~1.2 ms a frame on the render thread alone): one helper
// thread per view after the first, which the render thread scans itself.
struct HarvestHelpers {
  std::atomic<uint32_t> generation{0}, done{0};
  std::atomic<uint64_t> epoch{0};
  std::vector<std::thread> threads;
  void Start() {
    for (uint32_t i = 1; i < g_view_count; i++)
      threads.emplace_back([this, i] {
        uint32_t seen = 0;
        for (;;) {
          generation.wait(seen, std::memory_order_acquire);
          seen = generation.load(std::memory_order_acquire);
          HarvestOne(i, epoch.load(std::memory_order_acquire));
          done.fetch_add(1, std::memory_order_acq_rel);
          done.notify_all();
        }
      });
    for (auto& t : threads) t.detach();
  }
  void Run(uint64_t e) {
    done.store(0, std::memory_order_release);
    epoch.store(e, std::memory_order_release);
    generation.fetch_add(1, std::memory_order_acq_rel);
    generation.notify_all();
    HarvestOne(0, e);
    for (uint32_t d; (d = done.load(std::memory_order_acquire)) < threads.size();) done.wait(d);
  }
};
HarvestHelpers g_helpers;
std::once_flag g_helpers_started;
}  // namespace
#endif

void Harvest() {
  if (!Enabled()) return;
#ifdef __linux__
  auto start = std::chrono::steady_clock::now();
  uint64_t epoch = g_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
  std::call_once(g_helpers_started, [] { g_helpers.Start(); });
  g_helpers.Run(epoch);
  g_harvests++;
  g_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
#endif
}

uint64_t Watch(uint32_t physical, uint32_t size) {
  if (!g_ready || !size) return 0;
  physical &= kPhysicalSize - 1;
  if (uint64_t(physical) + size > kPhysicalSize) return 0;
#ifdef __linux__
  // Pages no copy was made from yet are not protected: protect them in every view (before the
  // caller reads them). UFFDIO_WRITEPROTECT also covers pages without page tables yet.
  uint32_t first = physical >> kPageShift, last = (physical + size - 1) >> kPageShift;
  auto* mem = REX_KERNEL_MEMORY();
  for (uint32_t p = first; p <= last;) {
    if (Interesting(p)) {
      p++;
      continue;
    }
    uint32_t run = p;
    while (p <= last && !Interesting(p)) g_interest[p >> 6] |= uint64_t(1) << (p & 63), p++;
    for (uint32_t i = 0; i < g_view_count; i++) {
      const View& v = g_views[i];
      uint32_t view_physical = v.physical;
      uint64_t lo = std::max<uint64_t>(uint64_t(run) << kPageShift, view_physical);
      uint64_t hi = std::min<uint64_t>(uint64_t(p) << kPageShift, uint64_t(view_physical) + v.size);
      if (lo >= hi) continue;
      uffdio_writeprotect wp{};
      wp.range.start = reinterpret_cast<uintptr_t>(v.host) + (lo - view_physical);
      wp.range.len = hi - lo;
      wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
      if (ioctl(g_uffd, UFFDIO_WRITEPROTECT, &wp) != 0) {
        static int logged = 0;
        if (logged++ < 5) REXLOG_INFO("[native] write watch: UFFDIO_WRITEPROTECT failed ({})", std::strerror(errno));
        for (uint32_t q = run; q < p; q++) g_interest[q >> 6] &= ~(uint64_t(1) << (q & 63));
        return 0;  // not protected everywhere: never clean
      }
    }
  }
#endif
  return g_epoch.load(std::memory_order_acquire);
}

bool Pending(uint32_t physical, uint32_t size) {
#ifdef __linux__
  if (!g_ready || !size) return false;
  physical &= kPhysicalSize - 1;
  auto* mem = REX_KERNEL_MEMORY();
  g_found = 0;
  for (uint32_t i = 0; i < g_view_count; i++) {
    const View& v = g_views[i];
    uint32_t view_physical = v.physical;
    uint64_t lo = std::max<uint64_t>(physical & ~0xFFFu, view_physical);
    uint64_t hi = std::min<uint64_t>(uint64_t(physical) + size, uint64_t(view_physical) + v.size);
    if (lo >= hi) continue;
    uint64_t host = reinterpret_cast<uintptr_t>(v.host) - view_physical;
    ScanView(v, host + lo, host + hi, false, 0);
  }
  return g_found != 0;
#else
  return false;
#endif
}

bool Clean(uint32_t physical, uint32_t size, uint64_t epoch) {
  if (!g_ready || !epoch || !size) return false;
  physical &= kPhysicalSize - 1;
  if (uint64_t(physical) + size > kPhysicalSize) return false;
  uint32_t first = physical >> kPageShift, last = (physical + size - 1) >> kPageShift;
  for (uint32_t p = first; p <= last; p++)
    if (g_page_epoch[p].load(std::memory_order_acquire) > epoch) return false;
  return true;
}

void Touch(uint32_t physical, uint32_t size) {
  if (!g_ready || !size) return;
  Mark(physical & (kPhysicalSize - 1), size, g_epoch.fetch_add(1, std::memory_order_acq_rel) + 1);
}

Stats TakeStats() {
  Stats s{g_harvests, g_pages.exchange(0), g_ms};
  g_harvests = 0;
  g_ms = 0;
  return s;
}

}  // namespace gtav::native::write_watch
