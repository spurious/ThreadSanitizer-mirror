//===-- tsan_rtl.h ----------------------------------------------*- C++ -*-===//
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
// Main internal TSan header file.
//
// Ground rules:
//   - C++ run-time should not be used (static CTORs, RTTI, exceptions, static
//     function-scope locals)
//   - All functions/classes/etc reside in namespace __tsan, except for those
//     declared in tsan_interface.h.
//   - Platform-specific files should be used instead of ifdefs (*).
//   - No system headers included in header files (*).
//   - Platform specific headres included only into platform-specific files (*).
//
//  (*) Except when inlining is critical for performance.
//===----------------------------------------------------------------------===//

#ifndef TSAN_RTL_H
#define TSAN_RTL_H

#define ALWAYS_INLINE  inline __attribute__ ((always_inline))
#define NOINLINE __attribute__ ((noinline))

namespace __tsan {

typedef unsigned long long u64;  // NOLINT
typedef unsigned long uptr;  // NOLINT

const uptr kPageSize = 4096;

#define CHECK(cond) \
  do { if (!cond) CheckFailed(__FILE__, __LINE__, #cond); \
  } while (false)

void CheckFailed(const char *file, int line, const char *cond);


// This struct is stored in TLS.
struct ThreadState {
  u64 tid              : 16;
  u64 epoch            : 40;
  u64 ignoring_reads   : 1;
  u64 ignoring_writes  : 1;
};


void InitializeShadowMemory();
void Printf(const char *format, ...);
void Report(const char *format, ...);
void Die();

// Extremely performance critical stuff. Must be inlined.
ThreadState GetThreadState();
void SetThreadState(ThreadState thread_state);
uptr GetShadowAddress(uptr application_address);

}  // namespace __tsan

#endif  // TSAN_RTL_H
