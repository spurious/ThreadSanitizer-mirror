//===-- tsan_interface_atomic.cc --------------------------------*- C++ -*-===//
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

#include "tsan_interface_atomic.h"
#include "tsan_placement_new.h"
#include "tsan_rtl.h"

using namespace __tsan;  // NOLINT

class ScopedAtomic {
 public:
  ScopedAtomic(ThreadState *thr, uptr pc, const char *func)
      : thr_(thr) {
    CHECK_EQ(thr_->in_rtl, 1);  // 1 due to our own ScopedInRtl member.
    DPrintf("#%d: %s\n", thr_->tid, func);
  }
  ~ScopedAtomic() {
    CHECK_EQ(thr_->in_rtl, 1);
  }
 private:
  ThreadState *thr_;
  ScopedInRtl in_rtl_;
};

#define SCOPED_ATOMIC(func, ...) \
    ThreadState *const thr = cur_thread(); \
    const uptr pc = (uptr)__builtin_return_address(0); \
    ScopedAtomic sa(thr, pc, __FUNCTION__); \
    return atomic_ ## func(thr, pc, __VA_ARGS__); \
/**/

// Some shortcuts.
typedef __tsan_memory_order morder;
typedef __tsan_atomic32 a32;
typedef __tsan_atomic64 a64;
const int mo_relaxed = __tsan_memory_order_relaxed;
const int mo_consume = __tsan_memory_order_consume;
const int mo_acquire = __tsan_memory_order_acquire;
const int mo_release = __tsan_memory_order_release;
const int mo_acq_rel = __tsan_memory_order_acq_rel;
const int mo_seq_cst = __tsan_memory_order_seq_cst;

template<typename T>
static T atomic_load(ThreadState *thr, uptr pc, const volatile T *a,
    morder mo) {
  CHECK(mo & (mo_relaxed | mo_consume | mo_acquire | mo_seq_cst));
  T v = *a;
  if (mo & (mo_consume | mo_acquire | mo_seq_cst))
    Acquire(thr, pc, (uptr)a);
  return v;
}

template<typename T>
static void atomic_store(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  CHECK(mo & (mo_relaxed | mo_release | mo_seq_cst));
  if (mo & (mo_release | mo_seq_cst))
    Release(thr, pc, (uptr)a);
  *a = v;
}

template<typename T>
static T atomic_exchange(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  if (mo & (mo_release | mo_acq_rel | mo_seq_cst))
    Release(thr, pc, (uptr)a);
  v = __sync_lock_test_and_set(a, v);
  if (mo & (mo_consume | mo_acquire | mo_acq_rel | mo_seq_cst))
    Acquire(thr, pc, (uptr)a);
  return v;
}

template<typename T>
static T atomic_fetch_add(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  if (mo & (mo_release | mo_acq_rel | mo_seq_cst))
    Release(thr, pc, (uptr)a);
  v = __sync_fetch_and_add(a, v);
  if (mo & (mo_consume | mo_acquire | mo_acq_rel | mo_seq_cst))
    Acquire(thr, pc, (uptr)a);
  return v;
}

template<typename T>
static bool atomic_compare_exchange_strong(ThreadState *thr, uptr pc,
    volatile T *a, T *c, T v, morder mo) {
  if (mo & (mo_release | mo_acq_rel | mo_seq_cst))
    Release(thr, pc, (uptr)a);
  T cc = *c;
  T pr = __sync_val_compare_and_swap(a, cc, v);
  if (mo & (mo_consume | mo_acquire | mo_acq_rel | mo_seq_cst))
    Acquire(thr, pc, (uptr)a);
  if (pr == cc)
    return true;
  *c = pr;
  return false;
}

static void atomic_thread_fence(ThreadState *thr, uptr pc, int unused) {
}

a32 __tsan_atomic32_load(const volatile a32 *a, morder mo) {
  SCOPED_ATOMIC(load, a, mo);
}

a64 __tsan_atomic64_load(const volatile a64 *a, morder mo) {
  SCOPED_ATOMIC(load, a, mo);
}

void __tsan_atomic32_store(volatile a32 *a, a32 v, morder mo) {
  SCOPED_ATOMIC(store, a, v, mo);
}

void __tsan_atomic64_store(volatile a64 *a, a64 v, morder mo) {
  SCOPED_ATOMIC(store, a, v, mo);
}

a32 __tsan_atomic32_exchange(volatile a32 *a, a32 v, morder mo) {
  SCOPED_ATOMIC(exchange, a, v, mo);
}

a64 __tsan_atomic64_exchange(volatile a64 *a, a64 v, morder mo) {
  SCOPED_ATOMIC(exchange, a, v, mo);
}

a32 __tsan_atomic32_fetch_add(volatile a32 *a, a32 v, morder mo) {
  SCOPED_ATOMIC(fetch_add, a, v, mo);
}

a64 __tsan_atomic64_fetch_add(volatile a64 *a, a64 v, morder mo) {
  SCOPED_ATOMIC(fetch_add, a, v, mo);
}

int __tsan_atomic32_compare_exchange_strong(volatile a32 *a, a32 *c, a32 v,
    morder mo) {
  SCOPED_ATOMIC(compare_exchange_strong, a, c, v, mo);
}

int __tsan_atomic64_compare_exchange_strong(volatile a64 *a, a64 *c, a64 v,
    morder mo) {
  SCOPED_ATOMIC(compare_exchange_strong, a, c, v, mo);
}

void __tsan_atomic_thread_fence(morder mo) {
  SCOPED_ATOMIC(thread_fence, 0);
}
