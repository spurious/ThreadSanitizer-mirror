#ifndef GCCTSAN_ATOMIC_H_INCLUDED
#define GCCTSAN_ATOMIC_H_INCLUDED

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


typedef struct __attribute__((aligned(8))) {
  uint64_t volatile             v;
} atomic_uint64_t;


uint64_t atomic_uint64_load(
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


void atomic_uint64_store(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(mo & (memory_order_relaxed
    | memory_order_release
    | memory_order_seq_cst));
  assert(((uintptr_t)a % 8) == 0);
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


uint64_t atomic_uint64_fetch_add(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_add(&a->v, v);
}


uint64_t atomic_uint64_fetch_and(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_and(&a->v, v);
}


uint64_t atomic_uint64_fetch_or(
  atomic_uint64_t* a,
  uint64_t v,
  memory_order mo) {
  assert(((uintptr_t)a % 8) == 0);
  return __sync_fetch_and_or(&a->v, v);
}


int atomic_uint64_compare_exchange(
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


typedef atomic_uint64_t               atomic_size_t;
#define atomic_size_store             atomic_uint64_store
#define atomic_size_load              atomic_uint64_load
#define atomic_size_fetch_add         atomic_uint64_fetch_add
#define atomic_size_fetch_and         atomic_uint64_fetch_and
#define atomic_size_fetch_or          atomic_uint64_fetch_or
#define atomic_size_compare_exchange  atomic_uint64_compare_exchange


#endif

