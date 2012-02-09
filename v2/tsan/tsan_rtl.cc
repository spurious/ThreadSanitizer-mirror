//===-- tsan_rtl.cc ---------------------------------------------*- C++ -*-===//
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
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "tsan_linux.h"
#include "tsan_rtl.h"
#include "tsan_interface.h"
#include "tsan_atomic.h"
#include <string.h>  // FIXME: remove me (for memcpy)

const int kTidBits = 16;
const int kMaxTid = 1 << kTidBits;
const int kClkBits = 40;

namespace __tsan {

struct Shadow {
  u64 tid   : kTidBits;
  u64 epoch : kClkBits;
  u64 addr0 : 3;
  u64 addr1 : 3;
  u64 write : 1;
};

static struct {
  int thread_seq;
} ctx;

u64 min(u64 a, u64 b) {
  return a < b ? a : b;
}

u64 max(u64 a, u64 b) {
  return a > b ? a : b;
}

static unsigned fastrand(ThreadState *thr) {
  return thr->rand = thr->rand * 1103515245 + 12345;
}

void CheckFailed(const char *file, int line, const char *cond) {
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\"\n",
         file, line, cond);
}

void Initialize() {
  Printf("tsan::Initialize\n");
  InitializeShadowMemory();
  // InitializeInterceptors();
}

int ThreadCreate(ThreadState *thr) {
  return ++ctx.thread_seq;
}

void ThreadStart(ThreadState *thr, int tid) {
  thr->id = tid;
  thr->epoch = 1;
}

static void ReportRace(ThreadState *thr, uptr addr,
                       Shadow s0, Shadow *s, int nrace) {
  (void)s0;
  (void)s;
  (void)nrace;
  Printf("#%d: RACE %p\n", thr->id, addr);
}

void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write) {
  atomic_uint64_t *shadow_mem = (atomic_uint64_t*)MemToShadow(addr);
  Printf("#%d: tsan::OnMemoryAccess: @%p %p size=%d"
         " is_write=%d shadow_mem=%p\n",
         (int)thr->id, (void*)pc, (void*)addr, size, is_write, shadow_mem);
  CHECK(IsAppMem(addr));
  CHECK(IsShadowMem((uptr)shadow_mem));

  // descriptor of the memory access
  Shadow s0 = {thr->id, thr->epoch, addr&7, min((addr&7)+size-1, 7), is_write};
  u64 s0v;
  memcpy(&s0v, &s0, sizeof(s0v));
  // is the descriptor already stored somewhere?
  bool replaced = false;
  // racy memory accesses
  Shadow races[kShadowCnt];
  int nrace = 0;

  // scan all the shadow values and dispatch to 4 categories:
  // same, replace, candidate and race (see comments below).
  // we consider only 3 cases regarding access sizes:
  // equal, intercept and not intercept. initially I considered
  // larger and smaller as well, it allowed to replace some
  // 'candidates' with 'same' or 'replace', but I think
  // it's just not worth it (performance- and complexity-wise).
  for (int i = 0; i < kShadowCnt; i++) {
    atomic_uint64_t *sp = &shadow_mem[i];
    u64 sv = atomic_load(sp, memory_order_relaxed);
    Printf("  [%d] %llx\n", i, sv);
    if (sv == 0) {
      if (replaced == false) {
        atomic_store(sp, s0v, memory_order_relaxed);
        replaced = true;
      }
      continue;
    }
    Shadow s;
    memcpy(&s, &sv, sizeof(s));
    // is the memory access equal to the previous?
    if (s0.addr0 == s.addr0 && s0.addr1 == s.addr1) {
      // same thread?
      if (s.tid == thr->id) {
        if (s.epoch >= thr->epoch) {
          if (s.write || !is_write) {
            // found a slot that holds effectively the same info
            // (that is, same tid, same sync epoch and same size)
            return;
          } else {
            atomic_store(sp, replaced ? 0ull : s0v, memory_order_relaxed);
            replaced = true;
            continue;
          }
        } else {
          if (!s.write || is_write) {
            atomic_store(sp, replaced ? 0ull : s0v, memory_order_relaxed);
            replaced = true;
            continue;
          } else {
            continue;
          }
        }
      } else {
        // happens before?
        if (thr->clock[s.tid] >= s.epoch) {
          atomic_store(sp, replaced ? 0ull : s0v, memory_order_relaxed);
          replaced = true;
          continue;
        } else if (!s.write && !is_write) {
          continue;
        } else {
          races[nrace++] = s;
          continue;
        }
      }
    // do the memory access intercept?
    } else if (min(s0.addr1, s.addr1) >= max(s0.addr0, s.addr0)) {
      if (s.tid == thr->id)
        continue;
      // happens before?
      if (thr->clock[s.tid] >= s.epoch) {
        continue;
      } else if (!s.write && !is_write) {
        continue;
      } else {
        races[nrace++] = s;
        continue;
      }
    }
    // the accesses do not intercept
    continue;
  }

  // find some races?
  if (nrace != 0)
    ReportRace(thr, addr, s0, races, nrace);
  // we did not find any races and had already stored
  // the current access info, so we are done
  if (replaced)
    return;
  // choose a random candidate slot and replace it
  unsigned i = fastrand(thr) % kShadowCnt;
  atomic_store(shadow_mem+i, s0v, memory_order_relaxed);
}

void FuncEntry(ThreadState *thr, uptr pc) {
  Printf("#%d: tsan::FuncEntry %p\n", (int)thr->id, (void*)pc);
}

void FuncExit(ThreadState *thr) {
  Printf("#%d: tsan::FuncExit\n", (int)thr->id);
}

}  // namespace __tsan

