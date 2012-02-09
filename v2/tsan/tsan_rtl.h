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

#include "tsan_clock.h"
#include "tsan_defs.h"
#include "tsan_slab.h"
#include "tsan_trace.h"

namespace __tsan {

// This struct is stored in TLS.
struct ThreadState {
  u64 id               : 16;
  u64 epoch            : 40;
  u64 ignoring_reads   : 1;
  u64 ignoring_writes  : 1;
  unsigned rand;
  TraceSet* trace;
  SlabCache* clockslab;
  VectorClock clock;
};

void InitializeShadowMemory();
void InitializeInterceptors();
void Printf(const char *format, ...);
void Report(const char *format, ...);
void Die() NORETURN;

void Initialize();
int ThreadCreate(ThreadState *thr);
void ThreadStart(ThreadState *thr, int tid);
void MutexCreate(ThreadState *thr, uptr addr, bool is_rw);
void MutexDestroy(ThreadState *thr, uptr addr);
void MutexLock(ThreadState *thr, uptr addr);
void MutexUnlock(ThreadState *thr, uptr addr);

// FIXME: Should be inlinable later (when things are settled down).
void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write);
void FuncEntry(ThreadState *thr, uptr pc);
void FuncExit(ThreadState *thr);

}  // namespace __tsan

#endif  // TSAN_RTL_H
