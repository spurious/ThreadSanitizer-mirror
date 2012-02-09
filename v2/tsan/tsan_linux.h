//===-- tsan_linux.h --------------------------------------------*- C++ -*-===//
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

#ifdef TSAN_SHADOW_STATE_LENGTH
const int kShadowCnt = TSAN_SHADOW_STATE_LENGTH;
#else
const int kShadowCnt = 8;
#endif

INLINE uptr MemToShadow(uptr addr) {
  return ((addr) & (~0x7c0000000003ULL)) * kShadowCnt;
}

static const uptr kLinuxShadowBeg = MemToShadow(kLinuxAppMemBeg);
static const uptr kLinuxShadowEnd =
  MemToShadow(kLinuxAppMemEnd) | (kPageSize - 1);

static inline bool IsShadowMem(uptr mem) {
  return mem >= kLinuxShadowBeg && mem <= kLinuxShadowEnd;
}

}  // namespace __tsan

#else  // __LP64__
# error "Only 64-bit is supported"
#endif

#endif  // __linux__
#endif  // TSAN_LINUX_H
