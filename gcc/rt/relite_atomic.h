/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
 * Author: Dmitry Vyukov (dvyukov@google.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
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

#ifndef RELITE_ATOMIC_H_INCLUDED
#define RELITE_ATOMIC_H_INCLUDED

#include <stdint.h>
#include <assert.h>


typedef enum {
  memory_order_relaxed          = 1 << 0,
  memory_order_consume          = 1 << 1,
  memory_order_acquire          = 1 << 2,
  memory_order_release          = 1 << 3,
  memory_order_acq_rel          = 1 << 4,
  memory_order_seq_cst          = 1 << 5,
} memory_order;


typedef struct __attribute__((aligned(4))) {
  uint32_t volatile             v;
} atomic_uint32_t;


typedef struct __attribute__((aligned(8))) {
  uint64_t volatile             v;
} atomic_uint64_t;


static inline uint32_t atomic_uint32_load (
  atomic_uint32_t const* a,
  memory_order mo) {
  assert(mo & (memory_order_relaxed
    | memory_order_consume
    | memory_order_acquire
    | memory_order_seq_cst));
  assert(((uintptr_t)a % sizeof(*a)) == 0);
  uint32_t v = a->v;
  __asm__ __volatile__ ("" ::: "memory");
  return v;
}


static inline uint64_t atomic_uint64_load (
  atomic_uint64_t const* a,
  memory_order mo) {
  assert(mo & (memory_order_relaxed
    | memory_order_consume
    | memory_order_acquire
    | memory_order_seq_cst));
  assert(((uintptr_t)a % 8) == 0);
  uint64_t v = a->v;
  __asm__ __volatile__ ("" ::: "memory");
  return v;
}


static inline void atomic_uint32_store(
  atomic_uint32_t* a,
  uint32_t v,
  memory_order mo) {
  assert(mo & (memory_order_relaxed
    | memory_order_release
    | memory_order_seq_cst));
  assert(((uintptr_t)a % sizeof(*a)) == 0);
  if (mo == memory_order_seq_cst) {
    uint32_t prev;
    __asm__ __volatile__("xchgl %0, %1"
      : "=r"(prev), "=m"(a->v)
      : "0"(v), "m"(a->v)
      : "memory", "cc");
  } else {
    __asm__ __volatile__ ("" ::: "memory");
    a->v = v;
    __asm__ __volatile__ ("" ::: "memory");
  }
}


static inline void atomic_uint64_store(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(mo & (memory_order_relaxed
    | memory_order_release
    | memory_order_seq_cst));
  assert(((uintptr_t)a % sizeof(*a)) == 0);
  if (mo == memory_order_seq_cst) {
    uint64_t prev;
    __asm__ __volatile__("xchgq %0, %1"
      : "=r"(prev), "=m"(a->v)
      : "0"(v), "m"(a->v)
      : "memory", "cc");
  } else {
    __asm__ __volatile__ ("" ::: "memory");
    a->v = v;
    __asm__ __volatile__ ("" ::: "memory");
  }
}


static inline uint64_t atomic_uint64_fetch_add(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_add(&a->v, v);
}


static inline uint64_t atomic_uint64_fetch_and(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_and(&a->v, v);
}


static inline uint64_t atomic_uint64_fetch_or(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_or(&a->v, v);
}


static inline int atomic_uint64_compare_exchange(
  atomic_uint64_t* a,
  uint64_t* cmp,
  uint64_t xchg,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  uint64_t cmpv = *cmp;
  uint64_t prev = __sync_val_compare_and_swap(&a->v, cmpv, xchg);
  if (cmpv == prev)
    return 1;
  *cmp = prev;
  return 0;
}


static inline int atomic_uint32_exchange(
  atomic_uint32_t* a,
  uint32_t v,
  memory_order mo) {
  assert(((uintptr_t)a % sizeof(*a)) == 0);
  uint32_t prev;
  __asm__ __volatile__("xchgl %0, %1"
    : "=r"(prev), "=m"(a->v)
    : "0"(v), "m"(a->v)
    : "memory", "cc");
  return prev;
}


typedef atomic_uint64_t               atomic_size_t;
#define atomic_size_store             atomic_uint64_store
#define atomic_size_load              atomic_uint64_load
#define atomic_size_fetch_add         atomic_uint64_fetch_add
#define atomic_size_fetch_and         atomic_uint64_fetch_and
#define atomic_size_fetch_or          atomic_uint64_fetch_or
#define atomic_size_compare_exchange  atomic_uint64_compare_exchange


#endif

