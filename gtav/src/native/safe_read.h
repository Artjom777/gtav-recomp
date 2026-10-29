#pragma once
// Copies memory that may be unmapped without faulting (false if any of it is unreadable):
// process_vm_readv on Linux/Android, mach_vm_read_overwrite on macOS. Not used on Windows.

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#elif !defined(_WIN32)
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace gtav::native {
#ifndef _WIN32
inline bool SafeReadMemory(void* dst, const void* src, size_t size) {
#if defined(__APPLE__)
  mach_vm_size_t got = 0;
  return mach_vm_read_overwrite(mach_task_self(), mach_vm_address_t(uintptr_t(src)), size,
                                mach_vm_address_t(uintptr_t(dst)), &got) == KERN_SUCCESS &&
         got == size;
#else
  iovec local{dst, size}, remote{const_cast<void*>(src), size};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
#endif
}
#endif
}  // namespace gtav::native
