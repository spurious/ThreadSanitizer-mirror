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
#include "tsan_sync.h"
#include "tsan_trace.h"

namespace __tsan {

void Printf(const char *format, ...);

// ThreadState:
//   tid             : kTidBits
//   epoch           : kClkBits
//   unused          :
//   ignore_bit      : 1
class FastState {
 public:
  FastState(u64 tid, u64 epoch) {
    x_ = tid << (64 - kTidBits);
    x_ |= epoch << (64 - kTidBits - kClkBits);
    CHECK(tid == this->tid());
    CHECK(epoch == this->epoch());
  }

  u64 tid() const {
    u64 res = x_ >> (64 - kTidBits);
    return res;
  }
  u64 epoch() const {
    u64 res = (x_ << kTidBits) >> (64 - kClkBits);
    return res;
  };
  void IncrementEpoch() {
    // u64 old_epoch = epoch();
    x_ += 1 << (64 - kTidBits - kClkBits);
    // CHECK(old_epoch + 1 == epoch());
  }
  void SetIgnoreBit() { x_ |= 1; }
  void ClearIgnoreBit() { x_ &= ~(u64)1; }
  bool GetIgnoreBit() { return x_ & 1; }
 private:
  friend class Shadow;
  explicit FastState(u64 x) : x_(x) { }
  u64 x_;
};

const int kSigCount = 1024;
const int kShadowStackSize = 1024;

struct my_siginfo_t {
  int opaque[128];
};

struct SignalDesc {
  bool armed;
  bool sigaction;
  my_siginfo_t siginfo;
};

// This struct is stored in TLS.
struct ThreadState {
  FastState fast_state;
  // Synch epoch represents the threads's epoch before the last synchronization
  // action. It allows to reduce number of shadow state updates.
  // For example, fast_synch_epoch=100, last write to addr X was at epoch=150,
  // if we are processing write to X from the same thread at epoch=200,
  // we do nothing, because both writes happen in the same 'synch epoch'.
  // That is, if another memory access does not race with the former write,
  // it does not race with the latter as well.
  // QUESTION: can we can squeeze this into ThreadState::Fast?
  // E.g. ThreadState::Fast is a 44-bit, 32 are taken by synch_epoch and 12 are
  // taken by epoch between synchs.
  // This way we can save one load from tls.
  u64 fast_synch_epoch;
  // This is a slow path flag. On fast path, fast_state.GetIgnoreBit() is read.
  // We do not distinguish beteween ignoring reads and writes
  // for better performance.
  int ignore_reads_and_writes;
  uptr *shadow_stack_pos;
  u64 *racy_shadow_addr;
  u64 racy_state[2];
  Trace trace;
  uptr shadow_stack[kShadowStackSize];
  SlabCache clockslab;
  SlabCache syncslab;
  ThreadClock clock;
  u64 stat[StatCnt];
  const int tid;
  int in_rtl;
  int func_call_count;
  const uptr stk_addr;
  const uptr stk_size;
  const uptr tls_addr;
  const uptr tls_size;

  DeadlockDetector deadlock_detector;

  int pending_signal_count;
  SignalDesc pending_signals[kSigCount];

  explicit ThreadState(Context *ctx, int tid, u64 epoch,
                       uptr stk_addr, uptr stk_size,
                       uptr tls_addr, uptr tls_size);
};

Context *CTX();
ReportDesc *GetGlobalReport();
extern __thread char cur_thread_placeholder[];

INLINE ThreadState *cur_thread() {
  return reinterpret_cast<ThreadState *>(&cur_thread_placeholder);
}

enum ThreadStatus {
  ThreadStatusInvalid,   // Non-existent thread, data is invalid.
  ThreadStatusCreated,   // Created but not yet running.
  ThreadStatusRunning,   // The thread is currently running.
  ThreadStatusFinished,  // Joinable thread is finished but not yet joined.
  ThreadStatusDead,      // Joined, but some info (trace) is still alive.
};

// An info about a thread that is hold for some time after its termination.
struct ThreadDeadInfo {
  Trace trace;
};

struct ThreadContext {
  const int tid;
  ThreadState *thr;
  ThreadStatus status;
  uptr uid;  // Some opaque user thread id.
  bool detached;
  int reuse_count;
  SyncClock sync;
  // Epoch at which the thread had started.
  // If we see an event from the thread stamped by an older epoch,
  // the event is from a dead thread that shared tid with this thread.
  u64 epoch0;
  u64 epoch1;
  StackTrace creation_stack;
  ThreadDeadInfo dead_info;
  ThreadContext* dead_next;  // In dead thread list.

  explicit ThreadContext(int tid);
};

struct Context {
  Context();

  bool initialized;

  SlabAlloc clockslab;
  SlabAlloc syncslab;
  SyncTab synctab;

  Mutex report_mtx;
  int nreported;
  int nmissed_expected;

  Mutex thread_mtx;
  int thread_seq;
  ThreadContext *threads[kMaxTid];
  int dead_list_size;
  ThreadContext* dead_list_head;
  ThreadContext* dead_list_tail;

  u64 stat[StatCnt];
};

class ScopedInRtl {
 public:
  ScopedInRtl();
  ~ScopedInRtl();
 private:
  ThreadState*thr_;
  int in_rtl_;
  int errno_;
};

void ALWAYS_INLINE INLINE StatInc(ThreadState *thr, StatType typ, u64 n = 1) {
  if (kCollectStats)
    thr->stat[typ] += n;
}

void InitializeShadowMemory();
void InitializeInterceptors();
void InitializePlatform();
void InitializeDynamicAnnotations();
void Report(const char *format, ...);
void Die() NORETURN;

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 1
# define DPrintf Printf
#else
# define DPrintf(...)
#endif

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 2
# define DPrintf2 Printf
#else
# define DPrintf2(...)
#endif

void Initialize(ThreadState *thr);
int Finalize(ThreadState *thr);

void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite);
void MemoryRead1Byte(ThreadState *thr, uptr pc, uptr addr);
void MemoryWrite1Byte(ThreadState *thr, uptr pc, uptr addr);
void MemoryRead8Byte(ThreadState *thr, uptr pc, uptr addr);
void MemoryWrite8Byte(ThreadState *thr, uptr pc, uptr addr);
void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr,
                       uptr size, bool is_write);
void MemoryResetRange(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryRangeFreed(ThreadState *thr, uptr pc, uptr addr, uptr size);
void IgnoreCtl(ThreadState *thr, bool write, bool begin);

void FuncEntry(ThreadState *thr, uptr pc);
void FuncExit(ThreadState *thr);

int ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached);
void ThreadStart(ThreadState *thr, int tid);
void ThreadFinish(ThreadState *thr);
void ThreadJoin(ThreadState *thr, uptr pc, uptr uid);
void ThreadDetach(ThreadState *thr, uptr pc, uptr uid);
void ThreadFinalize(ThreadState *thr);

void MutexCreate(ThreadState *thr, uptr pc, uptr addr, bool rw, bool recursive);
void MutexDestroy(ThreadState *thr, uptr pc, uptr addr);
void MutexLock(ThreadState *thr, uptr pc, uptr addr);
void MutexUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexReadLock(ThreadState *thr, uptr pc, uptr addr);
void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr);

void Acquire(ThreadState *thr, uptr pc, uptr addr);
void Release(ThreadState *thr, uptr pc, uptr addr);

void internal_memset(void *ptr, int c, uptr size);
void internal_memcpy(void *dst, const void *src, uptr size);
int internal_strcmp(const char *s1, const char *s2);

// The hacky call uses custom calling conversion and an assembly thunk.
// It is considerably faster that a normal call for the caller
// if it is not executed (it is intended for slow paths from hot functions).
// The trick is that the call preserves all registers and the compiler
// does not treat it as a call.
// If it does not work for you, use normal call.
#if TSAN_DEBUG == 0
#define HACKY_CALL(f) \
  __asm__ __volatile__("sub $0x1000, %%rsp;" \
                       "call " #f "_thunk;" \
                       "add $0x1000, %%rsp;" ::: "memory");
#else
#define HACKY_CALL(f) f()
#endif

extern "C" void __tsan_trace_switch();
void ALWAYS_INLINE INLINE TraceAddEvent(ThreadState *thr, u64 epoch,
                                        EventType typ, uptr addr) {
  StatInc(thr, StatEvents);
  if (UNLIKELY((epoch % kTracePartSize) == 0))
    HACKY_CALL(__tsan_trace_switch);
  Event *evp = &thr->trace.events[epoch % kTraceSize];
  Event ev = (u64)addr | ((u64)typ << 61);
  *evp = ev;
}

}  // namespace __tsan

#endif  // TSAN_RTL_H
