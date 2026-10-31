// Android: Bionic gives threads ~1 MB of stack by default, glibc 8 MB. The native renderer (and
// the rest of the game's host code) was written against the 8 MB default, and an overflow on
// Android kills the process without a crash report. Every pthread_create in libmain.so (std::thread
// included) is linked to this wrapper (-Wl,--wrap=pthread_create) and gets 8 MB unless the caller
// chose a size.
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <dirent.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

// The game's main thread does most of a frame's work (it waits ~20% of the time on an S23 FE;
// the renderer and the other game threads wait far more), but the scheduler ran it on the little
// cores ~45% of the time and never on the prime core. The renderer's threads keep to the middle
// cluster (renderer.cpp PinToBigCores), and this pins the main thread to the prime core (the last
// CPU). Checked every 250 ms, so it is live (--gtav_native_ab=gtav_android_main_prime=true|false).
REXCVAR_DEFINE_BOOL(gtav_android_main_prime, true, "GTAV",
                    "Android: run the game's main thread on the prime core (the last CPU)");

namespace {

bool SetAffinity(int tid, int first, int last) {
  cpu_set_t set;
  CPU_ZERO(&set);
  for (int i = first; i <= last; i++) CPU_SET(i, &set);
  return sched_setaffinity(tid, sizeof(set), &set) == 0;
}

// The SDK names guest threads after their entry ("Main XThread (F8000...)").
int FindMainThread() {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return 0;
  int found = 0;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    char path[64], name[32] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
    if (FILE* f = std::fopen(path, "r")) {
      size_t n = std::fread(name, 1, sizeof(name) - 1, f);
      name[n] = 0;
      std::fclose(f);
    }
    if (std::strncmp(name, "Main XThread", 12) == 0) {
      found = std::atoi(e->d_name);
      break;
    }
  }
  closedir(dir);
  return found;
}

const bool g_main_pinner = [] {
  std::thread([] {
    const int n = int(sysconf(_SC_NPROCESSORS_CONF));
    if (n < 4) return;
    int tid = 0, pinned = -1;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (!tid) tid = FindMainThread();
      if (!tid) continue;
      const int want = REXCVAR_GET(gtav_android_main_prime) ? 1 : 0;
      if (want == pinned) continue;
      if (!(want ? SetAffinity(tid, n - 1, n - 1) : SetAffinity(tid, 0, n - 1))) {
        tid = 0;  // gone (or a new run of it): look it up again
        continue;
      }
      if (pinned < 0) REXLOG_INFO("[android] main thread {} {}", tid, want ? "pinned to the prime core" : "unpinned");
      pinned = want;
    }
  }).detach();
  return true;
}();

}  // namespace

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg) {
  constexpr size_t kStackSize = 8u << 20;
  if (attr) {
    size_t size = 0;
    if (pthread_attr_getstacksize(attr, &size) == 0 && size >= kStackSize) {
      return __real_pthread_create(thread, attr, start, arg);
    }
    pthread_attr_t copy = *attr;
    pthread_attr_setstacksize(&copy, kStackSize);
    return __real_pthread_create(thread, &copy, start, arg);
  }
  pthread_attr_t own;
  pthread_attr_init(&own);
  pthread_attr_setstacksize(&own, kStackSize);
  int result = __real_pthread_create(thread, &own, start, arg);
  pthread_attr_destroy(&own);
  return result;
}
