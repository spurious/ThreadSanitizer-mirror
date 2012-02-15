//===-- tsan_platform.h -----------------------------------------*- C++ -*-===//
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
// Platform-specific code.
//===----------------------------------------------------------------------===//

#ifndef TSAN_LINUX_H
#define TSAN_LINUX_H
#ifdef __linux__

#include "tsan_rtl.h"

#if __LP64__
namespace __tsan {

static const uptr kLinuxAppMemBeg = 0x7f0000000000ULL;
static const uptr kLinuxAppMemEnd = 0x7fffffffffffULL;
static inline bool IsAppMem(uptr mem) {
  return mem >= kLinuxAppMemBeg && mem <= kLinuxAppMemEnd;
}

// This has to be a macro to allow constant initialization of constants below.
#define MemToShadow(addr) (((addr) & (~0x7c0000000007ULL)) * kShadowCnt)

static const uptr kLinuxShadowBeg = MemToShadow(kLinuxAppMemBeg);
static const uptr kLinuxShadowEnd =
  MemToShadow(kLinuxAppMemEnd) | (kPageSize - 1);

static inline bool IsShadowMem(uptr mem) {
  return mem >= kLinuxShadowBeg && mem <= kLinuxShadowEnd;
}

void *virtual_alloc(uptr size);
void virtual_free(void *p, uptr size);

}  // namespace __tsan

#else  // __LP64__
# error "Only 64-bit is supported"
#endif

#endif  // __linux__
#endif  // TSAN_LINUX_H
