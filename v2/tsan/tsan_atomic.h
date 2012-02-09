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

#include "tsan_defs.h"

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
  DCHECK(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  atomic_signal_fence(memory_order_seq_cst);
  u64 v = a->val_dont_use;
  atomic_signal_fence(memory_order_seq_cst);
  return v;
}

INLINE uptr atomic_load(const volatile atomic_uintptr_t *a, memory_order mo) {
  (void)mo;
  DCHECK(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  atomic_signal_fence(memory_order_seq_cst);
  uptr v = a->val_dont_use;
  atomic_signal_fence(memory_order_seq_cst);
  return v;
}

INLINE void atomic_store(volatile atomic_uint64_t *a, u64 v, memory_order mo) {
  (void)mo;
  DCHECK(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  atomic_signal_fence(memory_order_seq_cst);
  a->val_dont_use = v;
  atomic_signal_fence(memory_order_seq_cst);
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

INLINE void atomic_store(volatile atomic_uintptr_t *a, uptr v,
                         memory_order mo) {
  (void)mo;
  DCHECK(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  atomic_signal_fence(memory_order_seq_cst);
  a->val_dont_use = v;
  atomic_signal_fence(memory_order_seq_cst);
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

INLINE u64 atomic_fetch_add(volatile atomic_uint64_t *a, u64 v,
                            memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return __sync_fetch_and_add(&a->val_dont_use, v);
}

INLINE uptr atomic_fetch_add(volatile atomic_uintptr_t *a, uptr v,
                             memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return __sync_fetch_and_add(&a->val_dont_use, v);
}

INLINE uptr atomic_exchange(volatile atomic_uintptr_t *a, uptr v,
                            memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  uptr old = a->val_dont_use;
  for (;;) {
    uptr old2 = __sync_val_compare_and_swap(&a->val_dont_use, old, v);
    if (old == old2)
      return old;
    old = old2;
  }
}

}  // namespace __tsan

#endif  // TSAN_ATOMIC_H
