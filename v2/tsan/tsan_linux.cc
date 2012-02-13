//===-- tsan_linux.cc -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Linux-specific code.
//===----------------------------------------------------------------------===//

#include "tsan_linux.h"
#include "tsan_rtl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <map>

namespace __tsan {

void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void Report(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void internal_memset(void *ptr, int c, uptr size) {
  memset(ptr, c, size);  // FIXME: use REAL(memset) or a custom one.
}

void internal_memcpy(void *dst, const void *src, uptr size) {
  memcpy(dst, src, size);  // FIXME: use REAL(memcpy) or a custom one.
}

// FIXME: PoorMansMap should not use STL.
struct PoorMansMap::Impl {
  std::map<uptr, uptr> m;
};

PoorMansMap::PoorMansMap() { impl_ = new Impl; }
PoorMansMap::~PoorMansMap() { delete impl_; }
bool PoorMansMap::Insert(uptr key, uptr value) {
  return impl_->m.insert(std::make_pair(key, value)).second;
}
bool PoorMansMap::Erase(uptr key) {
  return impl_->m.erase(key);
}
bool PoorMansMap::Get(uptr key, uptr *value) {
  std::map<uptr, uptr>::iterator it = impl_->m.lower_bound(key);
  if (it == impl_->m.end()) return false;
  CHECK(it->first == key);
  *value = it->second;
  return true;
}



void Die() {
  _exit(1);
}

static void *my_mmap(void *addr, size_t length, int prot, int flags,
                    int fd, u64 offset) {
# if __WORDSIZE == 64
  return (void *)syscall(__NR_mmap, addr, length, prot, flags, fd, offset);
# else
  return (void *)syscall(__NR_mmap2, addr, length, prot, flags, fd, offset);
# endif
}

void *virtual_alloc(uptr size) {
  void *p = my_mmap(NULL, size, PROT_READ|PROT_WRITE,
      MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) {
    Report("FATAL: ThreadSanitizer can not allocate %lu MB\n", size<<20);
    Die();
  }
  return p;
}

void virtual_free(void *p, uptr size) {
  if (munmap(p, size)) {
    Report("FATAL: ThreadSanitizer munmap failed\n");
    Die();
  }
}

static void ProtectRange(uptr beg, uptr end) {
  if (beg != (uptr)my_mmap((void*)(beg), end - beg,
      PROT_NONE,
      MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE,
      -1, 0)) {
    Report("FATAL: ThreadSanitizer can not protect [%p,%p]\n", beg, end);
    Report("FATAL: Make sure you are not using unlimited stack\n");
    Die();
  }
}

void InitializeShadowMemory() {
  int stack;
  const uptr kClosedLowBeg  = 0x200000;
  const uptr kClosedLowEnd  = kLinuxShadowBeg - 1;
  const uptr kClosedHighBeg = kLinuxShadowEnd + 1;
  const uptr kClosedHighEnd = kLinuxAppMemBeg - 1;
  uptr shadow = (uptr)my_mmap((void*)kLinuxShadowBeg,
      kLinuxShadowEnd - kLinuxShadowBeg,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE,
      0, 0);
  if (shadow != kLinuxShadowBeg) {
    Report("FATAL: ThreadSanitizer can not mmap the shadow memory\n");
    Report("FATAL: Make shoure to compile with -fPIE and to link with -pie.\n");
    Die();
  }
  ProtectRange(kClosedLowBeg, kClosedLowEnd);
  ProtectRange(kClosedHighBeg, kClosedHighEnd);
  if (TSAN_DEBUG) {
    Printf("kClosedLowBeg   %p\n", kClosedLowBeg);
    Printf("kClosedLowEnd   %p\n", kClosedLowEnd);
    Printf("kLinuxShadowBeg %p\n", kLinuxShadowBeg);
    Printf("kLinuxShadowEnd %p\n", kLinuxShadowEnd);
    Printf("kClosedHighBeg  %p\n", kClosedHighBeg);
    Printf("kClosedHighEnd  %p\n", kClosedHighEnd);
    Printf("kLinuxAppMemBeg %p\n", kLinuxAppMemBeg);
    Printf("kLinuxAppMemEnd %p\n", kLinuxAppMemEnd);
    Printf("stack           %p\n", &stack);
    Printf("InitializeShadowMemory: %p %p\n", shadow);
  }
}

}  // namespace __tsan
