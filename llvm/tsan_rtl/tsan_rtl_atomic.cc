/* Copyright (c) 2010-2011, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Author: Dmitry Vyukov (dvyukov@google.com)

#include "tsan_rtl.h"
#include "../../gcc/plg/relite_rt_atomic.h"
#include "../../tsan/thread_sanitizer.h"
#include "../../earthquake/earthquake_core.h"


static uint64_t tsan_atomic_handle_op        (tsan_atomic_op op,
                                              tsan_memory_order mo,
                                              tsan_memory_order fail_mo,
                                              size_t size,
                                              void const volatile* a,
                                              uint64_t v,
                                              uint64_t cmp) {
  if (mo != tsan_memory_order_natomic) {
    shake_event_e shake_ev = op == tsan_atomic_op_load  ? shake_atomic_load :
                             op == tsan_atomic_op_store ? shake_atomic_store :
                             op == tsan_atomic_op_fence ? shake_atomic_fence :
                                                          shake_atomic_rmw;
    eq_sched_shake(shake_ev, a);
  }
  ENTER_RTL();
  uint64_t rv = ThreadSanitizerHandleAtomicOp(
      ExGetTid(),
      (uintptr_t)__builtin_return_address(0),
      op, mo, fail_mo, size, (void volatile*)a, v, cmp);
  LEAVE_RTL();
  return rv;
}


void      tsan_atomic_fence                     (tsan_memory_order mo) {
  tsan_atomic_handle_op(tsan_atomic_op_fence,
                        mo, tsan_memory_order_invalid,
                        1, 0, 0, 0);
}


uint64_t  tsan_atomic_load                      (void const volatile* a,
                                                 int size,
                                                 tsan_memory_order mo) {
  return tsan_atomic_handle_op(tsan_atomic_op_load,
                               mo, tsan_memory_order_invalid,
                               size, a, 0, 0);
}


void      tsan_atomic_store                     (void volatile* a,
                                                 int size,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  tsan_atomic_handle_op(tsan_atomic_op_store,
                        mo, tsan_memory_order_invalid,
                        size, a, v, 0);
}


uint64_t  tsan_atomic_fetch_op                  (void volatile* a,
                                                 int size,
                                                 uint64_t v,
                                                 tsan_memory_op op,
                                                 tsan_memory_order mo) {
  tsan_atomic_op aop = tsan_atomic_op_invalid;
  if (op == tsan_memory_op_xch)
    aop = tsan_atomic_op_exchange;
  else if (op == tsan_memory_op_add)
    aop = tsan_atomic_op_fetch_add;
  else if (op == tsan_memory_op_sub)
    aop = tsan_atomic_op_fetch_sub;
  else if (op == tsan_memory_op_and)
    aop = tsan_atomic_op_fetch_and;
  else if (op == tsan_memory_op_xor)
    aop = tsan_atomic_op_fetch_xor;
  else if (op == tsan_memory_op_or)
    aop = tsan_atomic_op_fetch_or;
  return tsan_atomic_handle_op(aop,
                               mo, tsan_memory_order_invalid,
                               size, a, v, 0);
}


uint64_t  tsan_atomic_compare_exchange          (void volatile* a,
                                                 int size,
                                                 int is_strong,
                                                 uint64_t cmp,
                                                 uint64_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo) {
  tsan_atomic_op aop = is_strong ?
      tsan_atomic_op_compare_exchange_strong :
      tsan_atomic_op_compare_exchange_weak;
  return tsan_atomic_handle_op(aop,
                               mo, fail_mo,
                               size, a, xch, cmp);
}




/*

static uint64_t tsan_atomic_handle_op        (tsan_atomic_op op,
                                              tsan_memory_order mo,
                                              tsan_memory_order fail_mo,
                                              size_t size,
                                              uint64_t volatile* a,
                                              uint64_t v,
                                              uint64_t cmp) {
  ENTER_RTL();
  uint64_t rv = ThreadSanitizerHandleAtomicOp(
      INFO.thread,
      (uintptr_t)__builtin_return_address(0),
      op, mo, fail_mo, size, a, v, cmp);
  LEAVE_RTL();
  return rv;
}


uint32_t  tsan_atomic32_load                    (uint32_t const volatile* a,
                                                 tsan_memory_order mo) {
  return (uint32_t)tsan_atomic_handle_op(
      tsan_atomic_load,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      0,
      0);
}


uint64_t  tsan_atomic64_load                    (uint64_t const volatile* a,
                                                 tsan_memory_order mo) {
  return (uint64_t)tsan_atomic_handle_op(
      tsan_atomic_load,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      0,
      0);
}


void      tsan_atomic32_store                   (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  tsan_atomic_handle_op(
      tsan_atomic_store,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


void      tsan_atomic64_store                   (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  tsan_atomic_handle_op(
      tsan_atomic_store,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


uint32_t  tsan_atomic32_exchange                (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  return (uint32_t)tsan_atomic_handle_op(
      tsan_atomic_exchange,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


uint64_t  tsan_atomic64_exchange                (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  return (uint64_t)tsan_atomic_handle_op(
      tsan_atomic_exchange,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


uint32_t  tsan_atomic32_fetch_add               (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  return (uint32_t)tsan_atomic_handle_op(
      tsan_atomic_fetch_add,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


uint64_t  tsan_atomic64_fetch_add               (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  return (uint64_t)tsan_atomic_handle_op(
      tsan_atomic_fetch_add,
      mo,
      tsan_memory_order_relaxed,
      sizeof(*a),
      (uint64_t*)a,
      v,
      0);
}


int       tsan_atomic32_compare_exchange_strong (uint32_t volatile* a,
                                                 uint32_t* cmp,
                                                 uint32_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo) {
  uint32_t cmpv = *cmp;
  uint32_t p = (uint32_t)tsan_atomic_handle_op(
      tsan_atomic_compare_exchange,
      mo,
      fail_mo,
      sizeof(*a),
      (uint64_t*)a,
      xch,
      cmpv);
  if (p == cmpv)
    return 1;
  *cmp = p; //TODO(dvyukov): handle as write to cmp
  return 0;
}


int       tsan_atomic64_compare_exchange_strong (uint64_t volatile* a,
                                                 uint64_t* cmp,
                                                 uint64_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo) {
  uint64_t cmpv = *cmp;
  uint64_t p = (uint64_t)tsan_atomic_handle_op(
      tsan_atomic_compare_exchange,
      mo,
      fail_mo,
      sizeof(*a),
      (uint64_t*)a,
      xch,
      cmpv);
  if (p == cmpv)
    return 1;
  *cmp = p; //TODO(dvyukov): handle as write to cmp
  return 0;
}


void      tsan_atomic_thread_fence              (tsan_memory_order mo) {
  tsan_atomic_handle_op(
      tsan_atomic_fence,
      mo,
      tsan_memory_order_relaxed,
      1,
      0,
      0,
      0);
}


*/

/*

struct partition_t {
  int volatile    lock;

};


struct padded_partition_t : partition_t {
  char            pad [128 - sizeof(partition_t)];
};


size_t const partition_count = 128;
static padded_partition_t g_partitions [partition_count];


#define ASSERT(x) if (x) {} else assert_fail(__LINE__, #x)
void assert_fail(int line, char const* expr) {
  fprintf(stderr, "TSAN ATOMIC ASSERT FAILED: (%s) @%d\n",
          expr, line);
}


static int is_acquire                           (tsan_memory_order mo) {
  return (mo & (tsan_memory_order_consume
      | tsan_memory_order_acquire
      | tsan_memory_order_acq_rel
      | tsan_memory_order_seq_cst));
}


static int is_release                           (tsan_memory_order mo) {
  return (mo & (tsan_memory_order_release
      | tsan_memory_order_acq_rel
      | tsan_memory_order_seq_cst));
}


static inline void handle_mop                   (void const volatile* a,
                                                 int size,
                                                 bool is_store) {
  ENTER_RTL();
  ThreadSanitizerThreadReportSuppression(INFO.thread, true);
  ThreadSanitizerHandleOneMemoryAccess(INFO.thread,
      MopInfo((uintptr_t)__builtin_return_address(0),
              size, is_store, true), (uintptr_t)a);
  ThreadSanitizerThreadReportSuppression(INFO.thread, false);
  LEAVE_RTL();
}


template<typename T>
static void handle_mop                          (T const volatile* a) {
  handle_mop(a, sizeof(*a), false);
}


template<typename T>
static void handle_mop                          (T volatile* a) {
  handle_mop(a, sizeof(*a), true);
}


uint32_t  tsan_atomic32_load                    (uint32_t const volatile* a,
                                                 tsan_memory_order mo) {
  return (uint32_t)
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  ASSERT(mo & (tsan_memory_order_relaxed
      | tsan_memory_order_consume
      | tsan_memory_order_acquire
      | tsan_memory_order_seq_cst));
  eq_sched_shake(shake_atomic_load, a);
  uint32_t v = *a;
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  handle_mop(a);
  return v;
}


uint64_t  tsan_atomic64_load                    (uint64_t const volatile* a,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  ASSERT(mo & (tsan_memory_order_relaxed
      | tsan_memory_order_consume
      | tsan_memory_order_acquire
      | tsan_memory_order_seq_cst));
  eq_sched_shake(shake_atomic_load, a);
  uint64_t v = *a;
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  handle_mop(a);
  return v;
}


void      tsan_atomic32_store                   (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  ASSERT(mo & (tsan_memory_order_relaxed
      | tsan_memory_order_release
      | tsan_memory_order_seq_cst));
  eq_sched_shake(shake_atomic_store, a);
  handle_mop(a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  if (mo == tsan_memory_order_seq_cst)
    __asm__ __volatile__ ("xchgl %1, %0" : "=r" (v) : "m" (*a), "0" (v));
  else
    *a = v;
}


void      tsan_atomic64_store                   (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  ASSERT(mo & (tsan_memory_order_relaxed
      | tsan_memory_order_release
      | tsan_memory_order_seq_cst));
  eq_sched_shake(shake_atomic_store, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
# ifdef __x86_64__
  if (mo == tsan_memory_order_seq_cst)
    __asm__ __volatile__ ("xchgq %1, %0" : "=r" (v) : "m" (*a), "0" (v));
  else
    *a = v;
#else
  uint64_t cmp = *a;
  while (!tsan_atomic64_compare_exchange_strong(a, &cmp, v, mo, mo)) {}
#endif
  handle_mop(a);
}


uint32_t  tsan_atomic32_exchange                (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  __asm__ __volatile__ ("xchgl %1, %0" : "=r" (v) : "m" (*a), "0" (v));
  handle_mop(a);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  return v;
}


uint64_t  tsan_atomic64_exchange                (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
# ifdef __x86_64__
  __asm__ __volatile__ ("xchgq %1, %0" : "=r" (v) : "m" (*a), "0" (v));
#else
  uint64_t cmp = *a;
  while (!tsan_atomic64_compare_exchange_strong(a, &cmp, v, mo, mo)) {}
  v = cmp;
#endif
  handle_mop(a);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  return v;
}


uint32_t  tsan_atomic32_fetch_add               (uint32_t volatile* a,
                                                 uint32_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  uint32_t prev = __sync_fetch_and_add(a, v);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  handle_mop(a);
  return prev;
}


uint64_t  tsan_atomic64_fetch_add               (uint64_t volatile* a,
                                                 uint64_t v,
                                                 tsan_memory_order mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  uint64_t prev = __sync_fetch_and_add(a, v);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  handle_mop(a);
  return prev;
}


int       tsan_atomic32_compare_exchange_strong (uint32_t volatile* a,
                                                 uint32_t* cmp,
                                                 uint32_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  uint32_t cmpv = *cmp;
  uint32_t prev = __sync_val_compare_and_swap(a, cmpv, xch);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  if (prev == cmpv)
    return 1;
  *cmp = prev;
  handle_mop(a);
  return 0;
}


int       tsan_atomic64_compare_exchange_strong (uint64_t volatile* a,
                                                 uint64_t* cmp,
                                                 uint64_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo) {
  ASSERT((((uintptr_t)a) % sizeof(*a)) == 0);
  eq_sched_shake(shake_atomic_rmw, a);
  if (is_release(mo))
    ExSPut(SIGNAL, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  uint64_t cmpv = *cmp;
  uint64_t prev = __sync_val_compare_and_swap(a, cmpv, xch);
  if (is_acquire(mo))
    ExSPut(WAIT, ExGetTid(), ExGetPc(), (uintptr_t)a, 0);
  if (prev == cmpv)
    return 1;
  *cmp = prev;
  handle_mop(a);
  return 0;
}


void      tsan_atomic_thread_fence              (tsan_memory_order mo) {
  ASSERT(mo != tsan_memory_order_relaxed);
  eq_sched_shake(shake_atomic_fence, 0);
  if (mo == tsan_memory_order_seq_cst)
    __sync_synchronize();
}

*/



