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
// Atomic operations. For now implies IA-32/Intel64.
//===----------------------------------------------------------------------===//

#ifndef TSAN_ATOMIC_H
#define TSAN_ATOMIC_H

#include "tsan_rtl.h"

namespace __tsan {

enum memory_order {
  memory_order_relaxed = 1 << 0,
  memory_order_consume = 1 << 1,
  memory_order_acquire = 1 << 2,
  memory_order_release = 1 << 3,
  memory_order_acq_rel = 1 << 4,
  memory_order_seq_cst = 1 << 5,
};

struct atomic_uint64_t {
  volatile u64 val_dont_use;
};

struct atomic_uintptr_t {
  volatile uptr val_dont_use;
};

INLINE void atomic_signal_fence(memory_order) {
  __asm__ __volatile__("" ::: "memory");
}

INLINE void atomic_thread_fence(memory_order) {
  __asm__ __volatile__("mfence" ::: "memory");
}

INLINE u64 atomic_load(const volatile atomic_uint64_t *a, memory_order mo) {
  (void)mo;
  assert(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  assert(((uptr)a % sizeof(*a)) == 0);
  atomic_signal_fence();
  uint64_t v = a->val_dont_use;
  atomic_signal_fence();
  return v;
}

INLINE uptr atomic_load(const volatile atomic_uintptr_t *a, memory_order mo) {
  (void)mo;
  assert(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  assert(((uptr)a % sizeof(*a)) == 0);
  atomic_signal_fence();
  uptr v = a->val_dont_use;
  atomic_signal_fence();
  return v;
}

INLINE void atomic_store(volatile atomic_uint64_t *a, u64 v, memory_order mo) {
  (void)mo;
  assert(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  assert(((uptr)a % sizeof(*a)) == 0);
  atomic_signal_fence();
  a->val_dont_use = v;
  atomic_signal_fence();
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

INLINE void atomic_store(volatile atomic_uintptr_t *a, uptr v,
                         memory_order mo) {
  (void)mo;
  assert(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  assert(((uptr)a % sizeof(*a)) == 0);
  atomic_signal_fence();
  a->val_dont_use = v;
  atomic_signal_fence();
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

INLINE u64 atomic_fetch_add(volatile atomic_uint64_t *a, u64 v,
                            memory_order mo) {
  (void)mo;
  assert(((uptr)a % sizeof(*a)) == 0);
  return __sync_fetch_and_add(&a->val_dont_use, v);
}

INLINE uptr atomic_fetch_add(volatile atomic_uintptr_t *a, uptr v,
                             memory_order mo) {
  (void)mo;
  assert(((uptr)a % sizeof(*a)) == 0);
  return __sync_fetch_and_add(&a->val_dont_use, v);
}

}  // namespace __tsan

#endif  // TSAN_ATOMIC_H
