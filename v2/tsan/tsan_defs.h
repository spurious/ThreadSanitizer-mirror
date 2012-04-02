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

#ifndef TSAN_DEBUG
#define TSAN_DEBUG 0
#endif  // TSAN_DEBUG

namespace __tsan {

typedef unsigned u32;  // NOLINT
typedef unsigned long long u64;  // NOLINT
typedef   signed long long s64;  // NOLINT
typedef unsigned long uptr;  // NOLINT

const uptr kPageSize = 4096;
const int kTidBits = 16;
const int kMaxTid = 1 << kTidBits;
const int kClkBits = 40;

#ifdef TSAN_SHADOW_STATE_LENGTH
const unsigned kShadowCnt = TSAN_SHADOW_STATE_LENGTH;
#else
const unsigned kShadowCnt = 8;
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

#define DCHECK_EQ(a, b) DCHECK((a) == (b))
#define DCHECK_NE(a, b) DCHECK((a) != (b))
#define DCHECK_LT(a, b) DCHECK((a) < (b))
#define DCHECK_LE(a, b) DCHECK((a) <= (b))
#define DCHECK_GT(a, b) DCHECK((a) > (b))
#define DCHECK_GE(a, b) DCHECK((a) >= (b))

void CheckFailed(const char *file, int line, const char *cond);

template<typename T>
T min(T a, T b) {
  return a < b ? a : b;
}

template<typename T>
T max(T a, T b) {
  return a > b ? a : b;
}

enum StatType {
  StatMop,
  StatMopRead,
  StatMopWrite,
  StatMop1,  // These must be consequtive.
  StatMop2,
  StatMop4,
  StatMop8,
  StatMopSame,
  StatMopRange,
  StatShadowProcessed,
  StatShadowZero,
  StatShadowNonZero,  // Derived.
  StatShadowSameSize,
  StatShadowIntersect,
  StatShadowNotIntersect,
  StatShadowSameThread,
  StatShadowAnotherThread,
  StatShadowReplace,
  StatFuncEnter,
  StatFuncExit,
  StatEvents,
  StatMtxTotal,
  StatMtxTrace,
  StatMtxThreads,
  StatMtxReport,
  StatMtxSyncVar,
  StatMtxSyncTab,
  StatMtxSlab,
  StatMtxAnnotations,
  StatMtxAtExit,
  StatCnt,
};

struct ThreadState;
struct ThreadContext;
struct Context;

}  // namespace __tsan

#endif  // TSAN_DEFS_H
