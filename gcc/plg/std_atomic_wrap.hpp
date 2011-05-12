/* ThreadSanitizer C++0x atomic wrappers
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
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

#pragma once
#include <atomic>
#include <stdio.h>


#define TSAN_WRAP(name, ...) __VA_ARGS__ __attribute__((tsan_replace(#name))); __VA_ARGS__


namespace tsan_wrapper {


tsan_memory_order convert (std::memory_order mo) {
  switch (mo) {
    case std::memory_order_relaxed: return tsan_memory_order_relaxed;
    case std::memory_order_consume: return tsan_memory_order_consume;
    case std::memory_order_acquire: return tsan_memory_order_acquire;
    case std::memory_order_release: return tsan_memory_order_release;
    case std::memory_order_acq_rel: return tsan_memory_order_acq_rel;
    case std::memory_order_seq_cst: return tsan_memory_order_seq_cst;
    default:                        return tsan_memory_order_invalid;
  }
}


// atomic_flag
TSAN_WRAP(std::__atomic2::atomic_flag::atomic_flag, void atomic_flag_init (std::__atomic2::atomic_flag* const a, bool v)) {
  tsan_atomic_store(a, sizeof(*a), v, tsan_memory_order_natomic);
}

TSAN_WRAP(std::__atomic2::atomic_flag::test_and_set, bool atomic_flag_test_and_set (std::__atomic2::atomic_flag* const a, std::memory_order mo)) {
  return (bool)tsan_atomic_fetch_op(a, sizeof(*a), true, tsan_memory_op_xch, convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_flag::clear, void atomic_flag_clear(std::__atomic2::atomic_flag* const a, std::memory_order mo)) {
  tsan_atomic_store(a, sizeof(*a), false, convert(mo));
}


// atomic_address
TSAN_WRAP(std::__atomic2::atomic_address::atomic_address, void atomic_address_init (std::__atomic2::atomic_address* const a, void* v)) {
  tsan_atomic_store(a, sizeof(*a), (uint64_t)v, tsan_memory_order_natomic);
}

TSAN_WRAP(std::__atomic2::atomic_address::load, void* atomic_address_load (std::__atomic2::atomic_address const* const a, std::memory_order mo)) {
  return (void*)tsan_atomic_load(a, sizeof(*a), convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_address::store, void atomic_address_store (std::__atomic2::atomic_address* const a, void* v, std::memory_order mo)) {
  tsan_atomic_store(a, sizeof(*a), v, convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_address::store, void atomic_address_store (std::__atomic2::atomic_address* const a, void* v, std::memory_order mo)) {
  tsan_atomic_store(a, sizeof(*a), v, convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_address::exchange, void* atomic_address_exchange (std::__atomic2::atomic_address* const a, void* v, std::memory_order mo)) {
  return (void*)tsan_atomic_fetch_op(a, sizeof(*a), (uint64_t)v, tsan_memory_op_xch, convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_address::fetch_add, void* atomic_address_fetch_add (std::__atomic2::atomic_address* const a, ptrdiff_t v, std::memory_order mo)) {
  return (void*)tsan_atomic_fetch_op(a, sizeof(*a), (uint64_t)v, tsan_memory_op_add, convert(mo));
}

TSAN_WRAP(std::__atomic2::atomic_address::fetch_sub, void* atomic_address_fetch_sub (std::__atomic2::atomic_address* const a, ptrdiff_t v, std::memory_order mo)) {
  return (void*)tsan_atomic_fetch_op(a, sizeof(*a), (uint64_t)v, tsan_memory_op_sub, convert(mo));
}



    bool
    compare_exchange_weak(void*& __v1, void* __v2, memory_order __m1,
			  memory_order __m2)
    { return compare_exchange_strong(__v1, __v2, __m1, __m2); }

    bool
    compare_exchange_weak(void*& __v1, void* __v2,
			  memory_order __m = memory_order_seq_cst)
    {
      return compare_exchange_weak(__v1, __v2, __m,
				   __calculate_memory_order(__m));
    }

    bool
    compare_exchange_strong(void*& __v1, void* __v2, memory_order __m1,
			    memory_order __m2)
    {
      __glibcxx_assert(__m2 != memory_order_release);
      __glibcxx_assert(__m2 != memory_order_acq_rel);
      __glibcxx_assert(__m2 <= __m1);

      void* __v1o = __v1;
      void* __v1n = __sync_val_compare_and_swap(&_M_i, __v1o, __v2);

      // Assume extra stores (of same value) allowed in true case.
      __v1 = __v1n;
      return __v1o == __v1n;
    }

    bool
    compare_exchange_strong(void*& __v1, void* __v2,
			  memory_order __m = memory_order_seq_cst)
    {
      return compare_exchange_strong(__v1, __v2, __m,
				     __calculate_memory_order(__m));
    }

    void*
    fetch_add(ptrdiff_t __d, memory_order __m = memory_order_seq_cst)
    { return __sync_fetch_and_add(&_M_i, __d); }

    void*
    fetch_sub(ptrdiff_t __d, memory_order __m = memory_order_seq_cst)
    { return __sync_fetch_and_sub(&_M_i, __d); }

    operator void*() const
    { return load(); }

    void*
    operator=(void* __v)
    {
      store(__v);
      return __v;
    }

    void*
    operator+=(ptrdiff_t __d)
    { return __sync_add_and_fetch(&_M_i, __d); }

    void*
    operator-=(ptrdiff_t __d)
    { return __sync_sub_and_fetch(&_M_i, __d); }



















TSAN_WRAP(std::__atomic2::__atomic_base<int>::load, int atomic_int_load (std::__atomic2::__atomic_base<int> const* const a, std::memory_order mo)) {
  return (int)tsan_atomic_load(a, sizeof(*a), convert(mo));
}

}

#undef TSAN_WRAP

