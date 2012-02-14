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

enum StatType {
  StatMop,
  StatFuncEnter,
  StatFuncExit,
  StatCnt,
};

// This struct is stored in TLS.
struct ThreadState {
  // The most performance-critical fields should fit into the
  // first 64 bit.
  union Fast {
    struct {
      u64 tid              : 16;
      u64 epoch            : 40;
      u64 ignoring_reads   : 1;
      u64 ignoring_writes  : 1;
    };
    u64 raw;
  };

  Fast fast;  // Should be the first field.
  u64 fast_synch_epoch;
  Trace trace;
  SlabCache* clockslab;
  VectorClock clock;
  u64 stat[StatCnt];
};

extern __thread ThreadState cur_thread;

void ALWAYS_INLINE INLINE StatInc(ThreadState *thr, StatType typ, u64 n = 1) {
  if (kCollectStats)
    thr->stat[typ] += n;
}

void InitializeShadowMemory();
void InitializeInterceptors();
void Printf(const char *format, ...);
void Report(const char *format, ...);
void Die() NORETURN;

void Initialize();
int ThreadCreate();
void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write);
void ThreadStart(int tid);
void ThreadStart(ThreadState *thr, int tid);
void MutexCreate(ThreadState *thr, uptr pc, uptr addr, bool is_rw);
void MutexDestroy(ThreadState *thr, uptr pc, uptr addr);
void MutexLock(ThreadState *thr, uptr pc, uptr addr);
void MutexUnlock(ThreadState *thr, uptr pc, uptr addr);

void internal_memset(void *ptr, int c, uptr size);
void internal_memcpy(void *dst, const void *src, uptr size);

class PoorMansMap {
 public:
  PoorMansMap();
  ~PoorMansMap();
  bool Insert(uptr key, uptr value);
  bool Erase(uptr key);
  bool Get(uptr key, uptr *value);
 private:
  struct Impl;
  Impl *impl_;
};

// Both K and V are pointer-sized PODs.
template <class K, class V>
class Map : private PoorMansMap {
 public:
  bool Insert(K key, V value) {
    return PoorMansMap::Insert((uptr)(key), (uptr)(value));
  }
  bool Erase(K key) {
    return PoorMansMap::Erase((uptr)(key));
  }
  bool Get(K key, V *value) {
    return PoorMansMap::Get((uptr)(key), (uptr*)(value));
  }
};


}  // namespace __tsan

#endif  // TSAN_RTL_H
