//===-- tsan_defs.h ---------------------------------------------*- C++ -*-===//
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
//===----------------------------------------------------------------------===//

#ifndef TSAN_DEFS_H
#define TSAN_DEFS_H

#include "tsan_compiler.h"
#include <stddef.h>  // NULL

#ifndef TSAN_DEBUG
#define TSAN_DEBUG 0
#endif  // TSAN_DEBUG

namespace __tsan {

typedef unsigned long long u64;  // NOLINT
typedef unsigned long uptr;  // NOLINT
typedef unsigned long size_t;  // NOLINT

const uptr kPageSize = 4096;
const int kTidBits = 8;
const int kMaxTid = 1 << kTidBits;
const int kClkBits = 40;

#ifdef TSAN_SHADOW_STATE_LENGTH
const int kShadowCnt = TSAN_SHADOW_STATE_LENGTH;
#else
const int kShadowCnt = 8;
#endif

#if defined(TSAN_COLLECT_STATS) && TSAN_COLLECT_STATS
const bool kCollectStats = true;
#else
const bool kCollectStats = false;
#endif

#define CHECK(cond) \
  do { if (!(cond)) ::__tsan::CheckFailed(__FILE__, __LINE__, #cond); \
  } while (false)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))

#if TSAN_DEBUG
# define DCHECK(cond) CHECK(cond)
#else
# define DCHECK(cond)
#endif

void CheckFailed(const char *file, int line, const char *cond);

}  // namespace __tsan

#endif  // TSAN_DEFS_H
