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

namespace __tsan {

typedef unsigned long long u64;  // NOLINT
typedef unsigned long uptr;  // NOLINT
typedef unsigned long size_t;  // NOLINT
typedef u64 clock_t;

const uptr kPageSize = 4096;
const int kTidBits = 8;
const int kMaxTid = 1 << kTidBits;
const int kClkBits = 40;

#define CHECK(cond) \
  do { if (!(cond)) ::__tsan::CheckFailed(__FILE__, __LINE__, #cond); \
  } while (false)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))

#ifdef NDEBUG
# define DCHECK(cond)
#else
# define DCHECK(cond) CHECK(cond)
#endif

void CheckFailed(const char *file, int line, const char *cond);

}  // namespace __tsan

#endif  // TSAN_DEFS_H
