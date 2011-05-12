#define TSAN_WRAP(name, ...) __VA_ARGS__ __attribute__((weak, tsan_replace(#name))); __VA_ARGS__

typedef int32_t tsan_Atomic32;
typedef int64_t tsan_Atomic64;
typedef intptr_t tsan_AtomicWord;
typedef int32_t tsan_AtomicRefCount;


TSAN_WRAP(base::subtle::NoBarrier_CompareAndSwap, tsan_Atomic32 tsan_NoBarrier_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_relaxed, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_AtomicExchange, tsan_Atomic32 tsan_NoBarrier_AtomicExchange(volatile tsan_Atomic32* ptr, tsan_Atomic32 new_value)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), new_value, tsan_memory_op_xch, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_AtomicIncrement, tsan_Atomic32 tsan_NoBarrier_AtomicIncrement(volatile tsan_Atomic32* ptr, tsan_Atomic32 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_relaxed) + increment;
}


TSAN_WRAP(base::subtle::Barrier_AtomicIncrement, tsan_Atomic32 tsan_Barrier_AtomicIncrement(volatile tsan_Atomic32* ptr, tsan_Atomic32 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_seq_cst) + increment;
}


TSAN_WRAP(base::subtle::Acquire_CompareAndSwap, tsan_Atomic32 tsan_Acquire_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_acquire, tsan_memory_order_acquire);
}


TSAN_WRAP(base::subtle::Release_CompareAndSwap, tsan_Atomic32 tsan_Release_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_release, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::MemoryBarrier, void tsan_MemoryBarrier()) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(base::subtle::NoBarrier_Store, void tsan_NoBarrier_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::Acquire_Store, void tsan_Acquire_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(base::subtle::Release_Store, void tsan_Release_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_release);
}


TSAN_WRAP(base::subtle::NoBarrier_Load, tsan_Atomic32 tsan_NoBarrier_Load(volatile const tsan_Atomic32* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::Acquire_Load, tsan_Atomic32 tsan_Acquire_Load(volatile const tsan_Atomic32* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
}


TSAN_WRAP(base::subtle::Release_Load, tsan_Atomic32 tsan_Release_Load(volatile const tsan_Atomic32* ptr)) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_CompareAndSwap, tsan_Atomic64 tsan_NoBarrier_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_relaxed, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_AtomicExchange, tsan_Atomic64 tsan_NoBarrier_AtomicExchange64(volatile tsan_Atomic64* ptr, tsan_Atomic64 new_value)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), new_value, tsan_memory_op_xch, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_AtomicIncrement, tsan_Atomic64 tsan_NoBarrier_AtomicIncrement64(volatile tsan_Atomic64* ptr, tsan_Atomic64 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_relaxed) + increment;
}


TSAN_WRAP(base::subtle::Barrier_AtomicIncrement, tsan_Atomic64 tsan_Barrier_AtomicIncrement64(volatile tsan_Atomic64* ptr, tsan_Atomic64 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_seq_cst) + increment;
}


TSAN_WRAP(base::subtle::Acquire_CompareAndSwap, tsan_Atomic64 tsan_Acquire_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_acquire, tsan_memory_order_acquire);
}


TSAN_WRAP(base::subtle::Release_CompareAndSwap, tsan_Atomic64 tsan_Release_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_release, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::NoBarrier_Store, void tsan_NoBarrier_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::Acquire_Store, void tsan_Acquire_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(base::subtle::Release_Store, void tsan_Release_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_release);
}


TSAN_WRAP(base::subtle::NoBarrier_Load, tsan_Atomic64 tsan_NoBarrier_Load64(volatile const tsan_Atomic64* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(base::subtle::Acquire_Load, tsan_Atomic64 tsan_Acquire_Load64(volatile const tsan_Atomic64* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
}


TSAN_WRAP(base::subtle::Release_Load, tsan_Atomic64 tsan_Release_Load64(volatile const tsan_Atomic64* ptr)) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}




TSAN_WRAP(base::AtomicRefCountDecN, char tsan_AtomicRefCountDecN(volatile tsan_AtomicRefCount* ptr, tsan_AtomicRefCount decrement)) {
  uint32_t v = tsan_atomic_fetch_op(ptr, sizeof(*ptr), decrement, tsan_memory_op_sub, tsan_memory_order_release);
  if ((tsan_AtomicRefCount)v == decrement) {
    tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
    return 0;
  }
  return 1;
}

TSAN_WRAP(base::AtomicRefCountIsOne, char tsan_AtomicRefCountIsOne(volatile tsan_AtomicRefCount* ptr)) {
  uint32_t v = tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
  if (v == 1) {
    tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
    return 1;
  }
  return 0;
}

TSAN_WRAP(base::AtomicRefCountIsZero, char tsan_AtomicRefCountIsZero(volatile tsan_AtomicRefCount* ptr)) {
  uint32_t v = tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
  if (v == 0) {
    tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
    return 1;
  }
  return 0;
}




TSAN_WRAP(v8::internal::NoBarrier_CompareAndSwap, tsan_Atomic32 tsan_v8_NoBarrier_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_relaxed, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_AtomicExchange, tsan_Atomic32 tsan_v8_NoBarrier_AtomicExchange(volatile tsan_Atomic32* ptr, tsan_Atomic32 new_value)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), new_value, tsan_memory_op_xch, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_AtomicIncrement, tsan_Atomic32 tsan_v8_NoBarrier_AtomicIncrement(volatile tsan_Atomic32* ptr, tsan_Atomic32 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_relaxed) + increment;
}


TSAN_WRAP(v8::internal::Barrier_AtomicIncrement, tsan_Atomic32 tsan_v8_Barrier_AtomicIncrement(volatile tsan_Atomic32* ptr, tsan_Atomic32 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_seq_cst) + increment;
}


TSAN_WRAP(v8::internal::Acquire_CompareAndSwap, tsan_Atomic32 tsan_v8_Acquire_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_acquire, tsan_memory_order_acquire);
}


TSAN_WRAP(v8::internal::Release_CompareAndSwap, tsan_Atomic32 tsan_v8_Release_CompareAndSwap(volatile tsan_Atomic32* ptr, tsan_Atomic32 old_value, tsan_Atomic32 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_release, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::MemoryBarrier, void tsan_v8_MemoryBarrier()) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(v8::internal::NoBarrier_Store, void tsan_v8_NoBarrier_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::Acquire_Store, void tsan_v8_Acquire_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(v8::internal::Release_Store, void tsan_v8_Release_Store(volatile tsan_Atomic32* ptr, tsan_Atomic32 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_release);
}


TSAN_WRAP(v8::internal::NoBarrier_Load, tsan_Atomic32 tsan_v8_NoBarrier_Load(volatile const tsan_Atomic32* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::Acquire_Load, tsan_Atomic32 tsan_v8_Acquire_Load(volatile const tsan_Atomic32* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
}


TSAN_WRAP(v8::internal::Release_Load, tsan_Atomic32 tsan_v8_Release_Load(volatile const tsan_Atomic32* ptr)) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_CompareAndSwap, tsan_Atomic64 tsan_v8_NoBarrier_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_relaxed, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_AtomicExchange, tsan_Atomic64 tsan_v8_NoBarrier_AtomicExchange64(volatile tsan_Atomic64* ptr, tsan_Atomic64 new_value)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), new_value, tsan_memory_op_xch, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_AtomicIncrement, tsan_Atomic64 tsan_v8_NoBarrier_AtomicIncrement64(volatile tsan_Atomic64* ptr, tsan_Atomic64 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_relaxed) + increment;
}


TSAN_WRAP(v8::internal::Barrier_AtomicIncrement, tsan_Atomic64 tsan_v8_Barrier_AtomicIncrement64(volatile tsan_Atomic64* ptr, tsan_Atomic64 increment)) {
  return tsan_atomic_fetch_op(ptr, sizeof(*ptr), increment, tsan_memory_op_add, tsan_memory_order_seq_cst) + increment;
}


TSAN_WRAP(v8::internal::Acquire_CompareAndSwap, tsan_Atomic64 tsan_v8_Acquire_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_acquire, tsan_memory_order_acquire);
}


TSAN_WRAP(v8::internal::Release_CompareAndSwap, tsan_Atomic64 tsan_v8_Release_CompareAndSwap64(volatile tsan_Atomic64* ptr, tsan_Atomic64 old_value, tsan_Atomic64 new_value)) {
  return tsan_atomic_compare_exchange(ptr, sizeof(*ptr), 1, old_value, new_value, tsan_memory_order_release, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::NoBarrier_Store, void tsan_v8_NoBarrier_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::Acquire_Store, void tsan_v8_Acquire_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_relaxed);
  tsan_atomic_fence(tsan_memory_order_seq_cst);
}


TSAN_WRAP(v8::internal::Release_Store, void tsan_v8_Release_Store64(volatile tsan_Atomic64* ptr, tsan_Atomic64 value)) {
  tsan_atomic_store(ptr, sizeof(*ptr), value, tsan_memory_order_release);
}


TSAN_WRAP(v8::internal::NoBarrier_Load, tsan_Atomic64 tsan_v8_NoBarrier_Load64(volatile const tsan_Atomic64* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}


TSAN_WRAP(v8::internal::Acquire_Load, tsan_Atomic64 tsan_v8_Acquire_Load64(volatile const tsan_Atomic64* ptr)) {
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_acquire);
}


TSAN_WRAP(v8::internal::Release_Load, tsan_Atomic64 tsan_v8_Release_Load64(volatile const tsan_Atomic64* ptr)) {
  tsan_atomic_fence(tsan_memory_order_seq_cst);
  return tsan_atomic_load(ptr, sizeof(*ptr), tsan_memory_order_relaxed);
}





#undef TSAN_WRAP


