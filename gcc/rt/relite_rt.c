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

#include "relite_rt.h"
#include "relite_thr.h"
#include "relite_atomic.h"
#include "relite_report.h"
#include "relite_hook.h"
#include "relite_stdlib.h"
#include "relite_dbg.h"
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <memory.h>
#include <sched.h>
#include <sys/mman.h>



// state encoding (64 bits):
// 1------- -------- -------- -------- -------- -------- -------- --------
//    synchronization variable - either mutex or atomic
//    low bits are a pointer to rl_rt_sync_t
// 0------- -------- -------- -------- -------- -------- -------- --------
//    plain variable, then
// -SZ----- -------- -------- -------- -------- -------- -------- --------
//    SZ == 0 -> 8 byte access (or unused for non 8 byte aligned addresses)
//    SZ == 1 -> 4 byte access
//    SZ == 2 -> 2 byte access
//    SZ == 3 -> 1 byte access
// ---L---- -------- -------- -------- -------- -------- -------- --------
//    L == 1 -> load
//    L == 0 -> store
// ----TTTT TTTTTTTT TTTT---- -------- -------- -------- -------- --------
//    T - thread index (16 bits)
// -------- -------- ----CCCC CCCCCCCC CCCCCCCC CCCCCCCC CCCCCCCC CCCCCCCC
//    C - timestamp (clock) (44 bits)


#define NOINLINE                __attribute__((noinline))
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
//#define LIKELY(x)               x
//#define UNLIKELY(x)             x


__thread void*                 relite_stack [64];
__thread void**                relite_func;


typedef struct rl_rt_context_t {
  void*                         shadow_mem;
  atomic_size_t                 thr_mask [THR_MASK_SIZE];
} rl_rt_context_t;


typedef struct rl_rt_sync_t {
  size_t                        clock_size;
  timestamp_t                   clock [MAX_THREADS];
} rl_rt_sync_t;


typedef struct debug_info_t {
  char const*                   file;
  int                           line;
  int                           pos;
} debug_info_t;


typedef struct static_func_desc_t {
  int                           call_count;
  debug_info_t const*           calls;
  int                           mop_count;
  debug_info_t const*           mops;
} static_func_desc_t;

/*
static debug_info_t relite_calls [] = {
    {"a", 1, 1},
    {"b", 2, 2},
};

static debug_info_t relite_mops [] = {
    {"a", 1, 1},
    {"b", 2, 2},
};

static static_func_desc_t relite_func_desc = {
    2, relite_calls, 2, relite_mops
};
*/

typedef struct dynamic_func_t {
  struct static_func_desc_t*    desc;
  struct dynamic_func_t*        parent;
  void*                         children;
} dynamic_func_t;


static                  rl_rt_context_t     g_ctx;
static __thread         relite_thr_t*       g_thr;


void handle_thread_start () {
  relite_thr_t* thr = relite_thr_init();
  assert(g_thr == 0);
  g_thr = thr;
  DBG("thread start %u", thr->id);
}


void                    handle_thread_end () {
  assert(g_thr != 0);
  relite_thr_t* thr = g_thr;
  DBG("thread end %u", thr->id);
  relite_thr_free(thr);
}


void            rl_rt_init      () __attribute__((constructor(101)));

void            rl_rt_init      () {
  if (g_ctx.shadow_mem == SHADOW_BASE)
    return;
  DBG("initializing...");
  g_ctx.shadow_mem = (atomic_uint64_t*)mmap(SHADOW_BASE,SHADOW_SIZE,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (g_ctx.shadow_mem != SHADOW_BASE)
    relite_fatal("failed to allocate shadow memory");
  handle_thread_start();
  DBG("initialization completed");
  relite_hook_init();
  relite_report_init();
}


/*
void __wrap___libc_csu_init() {
  rl_rt_init();
  extern void __real___libc_csu_init();
  __real___libc_csu_init();
}
*/

/*
void      rl_rt_thr_end       ()
{
  //atomic_bitmask_free(ctx.thr_mask, THR_MASK_SIZE, thr.id);
}
*/

unsigned                relite_rand         (unsigned limit) {
  return relite_thr_rand(g_thr, limit);
}


void                    relite_sched_shake  () {
#ifdef RELITE_SCHED_SHAKE
  relite_thr_t* thr = g_thr;
  unsigned rnd = relite_thr_rand(thr, 4);
  if (rnd == 0 || rnd == 1) {
    // nothing
  } else if (rnd == 2) {
    sched_yield();
  } else if (rnd == 3) {
    unsigned count = relite_thr_rand(thr, 10000);
    while (count --> 0) {
      __asm__ __volatile__ ("pause" ::: "memory");
    }
  }
#endif
}



static inline atomic_uint64_t* get_shadow(addr_t addr) {
  //TODO(dvyukov): how to eliminate it on fast-path?
  if (g_ctx.shadow_mem == 0)
    rl_rt_init();
  uintptr_t const offset = (uintptr_t)addr
    - (SHADOW_SIZE & ((uintptr_t)(addr < (addr_t)SHADOW_BASE) - 1));
  atomic_uint64_t* shadow = SHADOW_BASE + offset;
  return shadow;
}


static void   clock_assign_max    (timestamp_t* dest, timestamp_t const* src) {
  int i;
  for (i = 0; i != MAX_THREADS; i += 1) {
    if (dest[i] < src[i])
      dest[i] = src[i];
  }
}

/*
static int is_aligned(uintptr_t addr, size_t sz) {
  return ((addr & ((1 << (3 - sz)) - 1)) == 0);
}


static void handle_access_splitted(atomic_uint64_t* shadow,
                                  uintptr_t addr,
                                  size_t sz,
                                  int is_load) {
  assert(shadow != 0 && addr != 0);
  assert(is_aligned(addr, sz));
  assert(sz != SZ_8);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  if (UNLIKELY((state & STATE_SYNC_MASK) == 0)) {
    // something wicked
    return;
  }
  size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
}


static void handle_access_aligned(atomic_uint64_t* shadow,
                                  uintptr_t addr,
                                  size_t sz,
                                  int is_load) {
  assert(shadow != 0 && addr != 0);
  assert(is_aligned(addr, sz));
  assert(sz != SZ_8);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  if (UNLIKELY((state & STATE_SYNC_MASK) == 0)) {
    // something wicked
    return;
  }
  size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
  if (real_sz == SZ_8 && (addr % 8) != 0) {
    // unused as of now slot, covered by some previous slot
  }

  if (sz < real_sz) {
    assert(sz != SZ_1);
    if (sz == SZ_2) {
      handle_access_splitted(shadow,     addr,     SZ_1);
      handle_access_splitted(shadow + 1, addr + 1, SZ_1);
    } else {
      assert(sz == SZ_4);
      if (real_sz == SZ_2) {
        handle_access_splitted(shadow,     addr,     SZ_2);
        handle_access_splitted(shadow + 2, addr + 2, SZ_2);
      } else {
        assert(real_sz == SZ_1);
        handle_access_splitted(shadow,     addr,     SZ_1);
        handle_access_splitted(shadow + 1, addr + 1, SZ_1);
        handle_access_splitted(shadow + 2, addr + 2, SZ_1);
        handle_access_splitted(shadow + 3, addr + 3, SZ_1);
      }
    }
  } else {
    handle_access_splitted(shadow, addr, sz);
  }
}


static void handle_access_unaligned(atomic_uint64_t* shadow,
                                    uintptr_t addr,
                                    size_t sz,
                                    int is_load) {
  assert(shadow != 0 && addr != 0);
  if (is_aligned(addr, sz) == 0) {
    // the access is unaligned,
    // split it into several aligned accesses
    assert(sz != SZ_1);
    if (sz == SZ_2) {
      handle_access_aligned(shadow,     addr,     SZ_1, is_load);
      handle_access_aligned(shadow + 1, addr + 1, SZ_1, is_load);
    } else if (sz == SZ_4) {
      if (((uintptr_t)addr % 2) == 0) {
        handle_access_aligned(shadow,     addr,     SZ_2, is_load);
        handle_access_aligned(shadow + 2, addr + 2, SZ_2, is_load);
      } else {
        handle_access_aligned(shadow,     addr,     SZ_1, is_load);
        handle_access_aligned(shadow + 1, addr + 1, SZ_2, is_load);
        handle_access_aligned(shadow + 3, addr + 3, SZ_1, is_load);
      }
    } else {
      assert(sz == SZ_8);
      if (((uintptr_t)addr % 4) == 0) {
        handle_access_aligned(shadow,     addr,     SZ_4, is_load);
        handle_access_aligned(shadow + 4, addr + 4, SZ_4, is_load);
      } else if (((uintptr_t)addr % 2) == 0) {
        handle_access_aligned(shadow,     addr,     SZ_2, is_load);
        handle_access_aligned(shadow + 2, addr + 2, SZ_4, is_load);
        handle_access_aligned(shadow + 6, addr + 6, SZ_2, is_load);
      } else {
        handle_access_aligned(shadow,     addr,     SZ_1, is_load);
        handle_access_aligned(shadow + 1, addr + 1, SZ_2, is_load);
        handle_access_aligned(shadow + 3, addr + 3, SZ_4, is_load);
        handle_access_aligned(shadow + 7, addr + 7, SZ_1, is_load);
      }
    }
  } else {
    handle_access_aligned(shadow, addr, sz);
  }
}



static NOINLINE void handle_load_slow(atomic_uint64_t* shadow,
                                      addr_t addr,
                                      size_t sz) {
  handle_access_unaligned(shadow, (uintptr_t)addr, sz, 1);
}


static NOINLINE void handle_store_slow(atomic_uint64_t* shadow,
                                      addr_t addr,
                                      size_t sz) {
  handle_access_unaligned(shadow, (uintptr_t)addr, sz, 0);
}
*/



void            relite_load    (addr_t addr, unsigned flags) {
  int sz = 0;
  assert(addr != 0);
  assert(sz < 4);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("checking load at %p (flags=%u), state=%llx",
      addr, flags, (unsigned long long)state);
  // ensure that the address was not used as a sync variable
  if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
    //size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
    // ensure that the address is accessed with the expected size
    // and that the access is aligned
    //if (LIKELY((real_sz == sz) & is_aligned((uintptr_t)addr, sz))) {
      size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
      relite_thr_t* self = g_thr;
      // ensure that the previous access was from another thread
      if (UNLIKELY(prev_thrid != self->id)) {
        // ensure that the previous access was a store
        if (UNLIKELY((state & STATE_LOAD_MASK) == 0)) {
          timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
          // check for a race:
          // the previous store should happen before current load
          if (UNLIKELY(prev_ts > self->clock[prev_thrid])) {
            relite_report(addr, state, 1);
          }
          // calculate and store new state
          uint64_t const new_state = ((uint64_t)sz << STATE_SIZE_SHIFT)
            | STATE_LOAD_MASK | ((uint64_t)self->id << STATE_THRID_SHIFT)
            | self->clock[self->id];
          atomic_uint64_store(shadow, new_state, memory_order_relaxed);
        } else {
          // the previous access was a load,
          // so do nothing
        }
      } else {
        // the previous access was from the same thread,
        // if it was a load then update the timestamp
        // (if it was a store, then we better preserve the fact)
        if (LIKELY((state & STATE_LOAD_MASK) != 0)) {
          uint64_t const new_state = (state & ~STATE_TIMESTAMP_MASK)
            | self->clock[self->id];
          atomic_uint64_store(shadow, new_state, memory_order_relaxed);
        }
      }
    //} else {
    //  // the address is accessed with an unexpected size
    //  // or the access is unaligned,
    //  // so fall to slow-path to handle it
    //  handle_load_slow(shadow, addr, sz);
    //}
  } else {
    // the address was used as a sync variable,
    // so do not track races on it
  }
}


void            relite_store   (addr_t addr, unsigned flags) {
  int sz = 0;
  assert(addr != 0);
  assert(sz < 4);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("checking store at %p (flags=%u), state=%llx",
      addr, flags, (unsigned long long)state);
  relite_thr_t* self = g_thr;
  timestamp_t const own_clock = self->own_clock;
  // ensure that the address was not used as a sync variable
  if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
    //size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
    // ensure that the address is accessed with the expected size
    // and that the access is aligned
    //if (LIKELY((real_sz == sz) & is_aligned((uintptr_t)addr, sz))) {
      size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
      timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
      // ensure that the previous access was from another thread
      if (UNLIKELY(prev_thrid != self->id)) {
        // check for a race:
        // the previous access should happen before current store
        if (UNLIKELY(prev_ts > (self->clock[prev_thrid]))) {
          if (UNLIKELY(prev_ts != STATE_UNITIALIZED)) {
            relite_report(addr, state, 0);
          }
        }
        // calculate and store new state
        uint64_t const volatile new_state = ((uint64_t)sz << STATE_SIZE_SHIFT)
            | ((uint64_t)self->id << STATE_THRID_SHIFT)
            | self->clock[self->id];
        atomic_uint64_store(shadow, new_state, memory_order_relaxed);
      } else {
        if (UNLIKELY(prev_ts != own_clock)) {
          // calculate and store new state
          uint64_t const volatile new_state = ((uint64_t)sz << STATE_SIZE_SHIFT)
            | ((uint64_t)self->id << STATE_THRID_SHIFT)
            | self->clock[self->id];
          atomic_uint64_store(shadow, new_state, memory_order_relaxed);
        }
      }
    //} else {
    //  // the address is accessed with an unexpected size
    //  // or the access is unaligned,
    //  // so fall to slow-path to handle it
    //  handle_store_slow(shadow, addr, sz);
    //}
  } else {
    // the address was used as a sync variable,
    // so do not track races on it
  }
}


void                    handle_region_load  (void const volatile* begin,
                                             void const volatile* end) {
  //TODO(dvyukov): properly handle unaligned head and tail of the region
  assert(begin != 0 && begin <= end);
  DBG("checking region load %p-%p", begin, end);
  relite_thr_t* self = g_thr;
  uint64_t const my_ts = self->clock[self->id];
  uint64_t const state_templ = ((uint64_t)self->id << STATE_THRID_SHIFT)
      | STATE_LOAD_MASK
      | my_ts;
  timestamp_t const own_clock = self->own_clock;
  int is_race_detected = 0;
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
    // ensure that the address was not used as a sync variable
    if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
      // check that the address is occupied
      size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
      if (UNLIKELY(real_sz != SZ_8 || ((uintptr_t)shadow % 64) == 0)) {
        size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
        timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
        // ensure that the previous access was from another thread
        if (UNLIKELY(prev_thrid != self->id)) {
          // ensure that the previous access was a store
          if (LIKELY((state & STATE_LOAD_MASK) == 0)) {
            // check for a race:
            // the previous access should happen before current store
            if (UNLIKELY(prev_ts > (self->clock[prev_thrid]))) {
              if (is_race_detected == 0) {
                is_race_detected = 1;
                relite_report(begin + (shadow - get_shadow(begin)), state, 1);
              }
            }
            // calculate and store new state
            uint64_t const new_state = state_templ
                | ((uint64_t)real_sz << STATE_SIZE_SHIFT);
            atomic_uint64_store(shadow, new_state, memory_order_relaxed);
          } else {
            // the previous access was a load, so do nothing
          }
        } else {
          // the previous access was from the same thread,
          // if it was a load then update the timestamp
          // (if it was a store, then we better preserve the fact)
          if (LIKELY((state & STATE_LOAD_MASK) != 0)) {
            if (UNLIKELY(prev_ts != own_clock)) {
              uint64_t const new_state = (state & ~STATE_TIMESTAMP_MASK)
                  | my_ts;
              atomic_uint64_store(shadow, new_state, memory_order_relaxed);
            }
          }
        }
      }
    } else {
      // the address was used as a sync variable,
      // so do not track races on it
    }
  }
}


void                    handle_region_store (void const volatile* begin,
                                             void const volatile* end) {
  //TODO(dvyukov): properly handle unaligned head and tail of the region
  assert(begin != 0 && begin <= end);
  DBG("checking region store %p-%p", begin, end);
  relite_thr_t* self = g_thr;
  uint64_t const my_ts = self->clock[self->id];
  uint64_t const state_templ = ((uint64_t)self->id << STATE_THRID_SHIFT)
      | my_ts;
  int is_race_detected = 0;
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
    // ensure that the address was not used as a sync variable
    if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
      // check that the address is occupied
      size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
      if (UNLIKELY(real_sz != SZ_8 || ((uintptr_t)shadow % 64) == 0)) {
        size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
        // ensure that the previous access was from another thread
        if (UNLIKELY(prev_thrid != self->id)) {
          timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
          // check for a race:
          // the previous access should happen before current store
          if (UNLIKELY(prev_ts > (self->clock[prev_thrid]))) {
            if (UNLIKELY(prev_ts != STATE_UNITIALIZED)) {
              if (is_race_detected == 0) {
                is_race_detected = 1;
                relite_report(begin + (shadow - get_shadow(begin)), state, 0);
              }
            }
          }
        }
        // calculate and store new state
        uint64_t const new_state = state_templ
            | ((uint64_t)real_sz << STATE_SIZE_SHIFT);
        atomic_uint64_store(shadow, new_state, memory_order_relaxed);
      }
    } else {
      // the address was used as a sync variable,
      // so do not track races on it
    }
  }
}


void                    handle_mem_init     (addr_t begin,
                                             addr_t end,
                                             state_t state) {
  assert(begin != 0 && begin <= end);
  uint64_t const state_templ = state | ((state_t)SZ_1 << STATE_SIZE_SHIFT);
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    atomic_uint64_store(shadow, state_templ, memory_order_relaxed);
  }
}


void                    handle_mem_alloc    (addr_t begin,
                                             addr_t end) {
  assert(begin != 0 && begin <= end);
  relite_thr_t* self = g_thr;
  uint64_t const state_templ = ((uint64_t)SZ_8 << STATE_SIZE_SHIFT)
    | ((uint64_t)self->id << STATE_THRID_SHIFT)
    | self->clock[self->id];
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    atomic_uint64_store(shadow, state_templ, memory_order_relaxed);
  }
}


void                    handle_mem_free     (addr_t begin,
                                             addr_t end) {
  //TODO(dvyukov): properly handle unaligned head and tail of the region
  assert(begin != 0 && begin <= end);
  relite_thr_t* self = g_thr;
  uint64_t const state_templ = STATE_FREED
      | ((state_t)SZ_1 << STATE_SIZE_SHIFT);
  int is_race_detected = 0;
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
    // ensure that the address was not used as a sync variable
    if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
      timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
      if (LIKELY(prev_ts != STATE_MINE_ZONE && prev_ts != STATE_UNITIALIZED)) {
        size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
        // check for a race:
        // the previous access should happen before current store
        if (UNLIKELY(prev_ts > (self->clock[prev_thrid]))) {
          if (is_race_detected == 0) {
            is_race_detected = 1;
            relite_report(begin + (shadow - get_shadow(begin)), state, 0);
          }
        }
      }
    } else {
      //TODO(dvyukov): release sync object
    }
    atomic_uint64_store(shadow, state_templ, memory_order_relaxed);
  }
}


void            handle_sync_create   (addr_t addr) {
  DBG("sync_create at %p", addr);
  rl_rt_sync_t* sync = relite_malloc(sizeof(rl_rt_sync_t));
  if (sync == 0)
    return;
  relite_memset(sync, 0, sizeof(rl_rt_sync_t));
  //!!! assert(sync);
  assert(((uint64_t)sync & STATE_SYNC_MASK) == 0);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t new_state = STATE_SYNC_MASK | (uint64_t)sync;
  atomic_uint64_store(shadow, new_state, memory_order_relaxed);
}


void                    handle_sync_destroy (addr_t addr) {
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("sync_destroy at %p, state=%llx", addr, (unsigned long long)state);
  if ((state & STATE_SYNC_MASK) == 0)
    return;
  rl_rt_sync_t* sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  assert(sync != 0);
  relite_free(sync);
  //TODO(dvyukov): mute use CAS,
  // otherwise 2 threads(rl_rt_sync_t*)(state & ~STATE_SYNC_MASK) can free sync simultaneously
}


void            handle_sync_acquire  (addr_t addr, int is_mtx) {
  //TODO(dvyukov): add scheduler shake
  // however, it should be placed *before* the load
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("acquire at %p, state=%llx", addr, (unsigned long long)state);
  if ((state & STATE_SYNC_MASK) == 0)
    return;
  rl_rt_sync_t* sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  relite_thr_t* self = g_thr;
  clock_assign_max(self->clock, sync->clock);
}


void            handle_sync_release  (addr_t addr, int is_mtx) {
  if (is_mtx == 0)
    relite_sched_shake();
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("release at %p, state=%llx", addr, (unsigned long long)state);
  rl_rt_sync_t* sync;
  if ((state & STATE_SYNC_MASK) == 0) {
    sync = relite_malloc(sizeof(rl_rt_sync_t));
    if (sync == 0)
      return;
    relite_memset(sync, 0, sizeof(rl_rt_sync_t));
    uint64_t new_state = STATE_SYNC_MASK | (uint64_t)sync;
    //TODO(dvyukov): perhaps it's better to do that with CAS
    // in order to prevent potential races and memory leaks
    atomic_uint64_store(shadow, new_state, memory_order_relaxed);
  } else {
    sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  }
  relite_thr_t* self = g_thr;
  self->own_clock += 1;
  self->clock[self->id] += 1;
  clock_assign_max(sync->clock, self->clock);
}


void    relite_acquire                (void const volatile* addr) {
  handle_sync_acquire(addr, 0);
}


void    relite_release                (void const volatile* addr) {
  handle_sync_release(addr, 0);
}


void relite_enter(void const volatile* p)
{
  //DBG("enter %p", p);
}


void relite_leave()
{
  //DBG("leave");
}













/*
void AnnotateExpectRace(char const* file, int line, void* address, char const* description) {

}
*/








