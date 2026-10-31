// Android: Bionic gives threads ~1 MB of stack by default, glibc 8 MB. The native renderer (and
// the rest of the game's host code) was written against the 8 MB default, and an overflow on
// Android kills the process without a crash report. Every pthread_create in libmain.so (std::thread
// included) is linked to this wrapper (-Wl,--wrap=pthread_create) and gets 8 MB unless the caller
// chose a size.
#include <pthread.h>

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
