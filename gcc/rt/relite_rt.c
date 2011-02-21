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
#include "relite_atomic.h"
#include "relite_dbg.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <memory.h>
//#include <errno.h>
#include <unistd.h>
#include <execinfo.h>
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


#define MAX_THREADS             (64*1024)
#define THR_MASK_SIZE           (MAX_THREADS / sizeof(size_t) / 8)
#define SHADOW_BASE             ((atomic_uint64_t*)0x00000D0000000000ull)
#define SHADOW_SIZE             (0x0000800000000000ull - 0x00000E38E38E3800ull)
#define STATE_SYNC_MASK         0x8000000000000000ull
#define STATE_SYNC_SHIFT        63
#define STATE_SIZE_MASK         0x6000000000000000ull
#define STATE_SIZE_SHIFT        61
#define STATE_LOAD_MASK         0x1000000000000000ull
#define STATE_LOAD_SHIFT        60
#define STATE_THRID_MASK        0x0FFFF00000000000ull
#define STATE_THRID_SHIFT       44
#define STATE_TIMESTAMP_MASK    0x00000FFFFFFFFFFFull

#define SZ_1                    3
#define SZ_2                    2
#define SZ_4                    1
#define SZ_8                    0

#define NOINLINE                __attribute__((noinline))
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
//#define LIKELY(x)               x
//#define UNLIKELY(x)             x


typedef     void const volatile*addr_t;
typedef     uint32_t            thrid_t;
typedef     uint64_t            timestamp_t;
typedef     uint64_t            state_t;




__thread void*                 relite_stack [64];
__thread void**                relite_func;


typedef struct rl_rt_context_t {
  atomic_size_t                 thr_mask [THR_MASK_SIZE];
} rl_rt_context_t;


typedef struct rl_rt_thread_t {
  thrid_t                       id;
  size_t                        clock_size;
  timestamp_t                   clock [MAX_THREADS];
} rl_rt_thread_t;


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


static rl_rt_context_t          ctx;
static __thread rl_rt_thread_t  thr;


#ifdef RELITE_RT_DUMP
static pthread_mutex_t g_dbg_mtx = PTHREAD_MUTEX_INITIALIZER;
void dump_clock(char const* desc, timestamp_t const* clock) {
  pthread_mutex_lock(&g_dbg_mtx);
  fprintf(stderr, "relite(%u): %s:", thr.id, desc);
  int i;
  for (i = 0; i != MAX_THREADS; i += 1) {
    if (clock[i] != 0)
      fprintf(stderr, " %d=%llu", i, (unsigned long long)(clock[i]));
  }
  fprintf(stderr, "\n");
  pthread_mutex_unlock(&g_dbg_mtx);
}
#else
void dump_clock(char const* desc, timestamp_t const* clock) {
  (void)desc;
  (void)clock;
}
#endif

static void fatal(char const* msg) __attribute__((noreturn));
static void fatal(char const* msg) {
  fprintf(stderr, "rl_rt: %s\n", msg);
  exit(1);
}


static size_t atomic_bitmask_alloc(atomic_size_t* mask, size_t size) {
  size_t slot = 0;
  for (; slot != size; slot += 1) {
    size_t cmp = atomic_size_load(&mask[slot], memory_order_relaxed);
    for (;;) {
      if (cmp == (size_t)-1)
        break;
      size_t xchg;
      size_t idx = 0;
      for (; idx != 8 * sizeof(size_t); idx += 1) {
        xchg = cmp | (1ull << idx);
        if (cmp ^ xchg)
          break;
      }
      if (atomic_size_compare_exchange
          (&mask[slot], &cmp, xchg, memory_order_relaxed))
        return slot * sizeof(size_t) * 8 + idx;
    }
  }
  fatal("too many threads");
}

/*
static void atomic_bitmask_free(atomic_size_t* mask, size_t size, size_t bit) {
  size_t const slot = bit / (8 * sizeof(size_t));
  size_t const idx = bit % (8 * sizeof(size_t));
  size_t const add = (size_t)(1ull << idx);
  assert(slot < size);
  assert((atomic_size_load(&mask[slot], memory_order_relaxed) & add) != 0);
  atomic_size_fetch_and(&mask[slot], ~add, memory_order_relaxed);
}
*/


void handle_thread_start () {
  thr.id = atomic_bitmask_alloc(ctx.thr_mask, THR_MASK_SIZE);
  thr.clock_size = thr.id;
  thr.clock[thr.id] = 1;
  DBG("thread start %u", thr.id);
}


void                    handle_thread_end () {

}


void            rl_rt_init      () __attribute__((constructor(101)));
void            rl_rt_init      () {
  DBG("initalizing");
  void* mem = (atomic_uint64_t*)mmap(SHADOW_BASE, SHADOW_SIZE,
    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mem != SHADOW_BASE)
    fatal("failed to allocate shadow memory");
  // reserve thread index 0
  atomic_size_store(&ctx.thr_mask[0], 1, memory_order_relaxed);
  handle_thread_start();
}


void      rl_rt_thr_end       ()
{
  //atomic_bitmask_free(ctx.thr_mask, THR_MASK_SIZE, thr.id);
}






static atomic_uint64_t* get_shadow(addr_t addr) {
  uintptr_t const offset = (uintptr_t)addr
    - (SHADOW_SIZE & ((uintptr_t)(addr < (addr_t)SHADOW_BASE) - 1));
  atomic_uint64_t* shadow = SHADOW_BASE + offset;
  return shadow;
}


static void handle_race(addr_t addr, size_t sz, int is_load, int is_load2) {
  void* stacktrace [64];
  int stacktrace_size = backtrace(
      stacktrace, sizeof(stacktrace)/sizeof(stacktrace[0]));
  char** stacktrace_sym =
      backtrace_symbols(stacktrace, stacktrace_size);
  int i;
  for (i = 0; i != stacktrace_size; i += 1) {
    DBG("#%d %s", i, stacktrace_sym[i]);
    char syscom [1024];
    snprintf(syscom, sizeof(syscom)/sizeof(syscom[0]),
             "addr2line -Cf -e utest %p", stacktrace[i]);
    system(syscom);
  }
  free(stacktrace_sym);



  unsigned real_size = 8;
  if (sz == 1)
    real_size = 4;
  else if (sz == 2)
    real_size = 2;
  else if (sz == 3)
    real_size = 1;
  fprintf(stderr, "%s-%s race on %p (%u bytes)\n",
    (is_load ? "load" : "store"),
    (is_load2 ? "load" : "store"),
    addr, real_size);

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



void            relite_load    (addr_t addr) {
  int sz = 0;
  assert(addr != 0);
  assert(sz < 4);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("checking load at %p, state=%llx", addr, (unsigned long long)state);
  // ensure that the address was not used as a sync variable
  if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
    //size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
    // ensure that the address is accessed with the expected size
    // and that the access is aligned
    //if (LIKELY((real_sz == sz) & is_aligned((uintptr_t)addr, sz))) {
      size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
      rl_rt_thread_t* self = &thr;
      // ensure that the previous access was from another thread
      if (UNLIKELY(prev_thrid != self->id)) {
        // ensure that the previous access was a store
        if (UNLIKELY((state & STATE_LOAD_MASK) == 0)) {
          timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
          // check for a race:
          // the previous store should happen before current load
          if (UNLIKELY(prev_ts > self->clock[prev_thrid]))
            handle_race(addr, sz, 1, 0);
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


void            relite_store   (addr_t addr) {
  int sz = 0;
  assert(addr != 0);
  assert(sz < 4);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("checking store at %p, state=%llx", addr, (unsigned long long)state);
  dump_clock("thread", thr.clock);
  // ensure that the address was not used as a sync variable
  if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
    //size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
    // ensure that the address is accessed with the expected size
    // and that the access is aligned
    //if (LIKELY((real_sz == sz) & is_aligned((uintptr_t)addr, sz))) {
      size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
      rl_rt_thread_t* self = &thr;
      // ensure that the previous access was from another thread
      if (UNLIKELY(prev_thrid != self->id)) {
        timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
        // check for a race:
        // the previous access should happen before current store
        if (UNLIKELY(prev_ts > (self->clock[prev_thrid])))
          handle_race(addr, sz, 0, (state & STATE_LOAD_MASK) != 0);
      }
      // calculate and store new state
      uint64_t const new_state = ((uint64_t)sz << STATE_SIZE_SHIFT)
        | ((uint64_t)self->id << STATE_THRID_SHIFT)
        | self->clock[self->id];
      atomic_uint64_store(shadow, new_state, memory_order_relaxed);
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
  assert(begin != 0 && begin <= end);
  DBG("checking region load %p-%p", begin, end);
  rl_rt_thread_t* self = &thr;
  uint64_t const my_ts = self->clock[self->id];
  uint64_t const state_templ = ((uint64_t)self->id << STATE_THRID_SHIFT)
      | STATE_LOAD_MASK
      | my_ts;
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
    // ensure that the address was not used as a sync variable
    if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
      // check that the address is occupied
      size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
      if (real_sz != SZ_8 || ((uintptr_t)shadow % 64) == 0) {
        size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
        // ensure that the previous access was from another thread
        if (UNLIKELY(prev_thrid != self->id)) {
          // ensure that the previous access was a store
          if (LIKELY((state & STATE_LOAD_MASK) == 0)) {
            // check for a race:
            // the previous access should happen before current store
            timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
            if (UNLIKELY(prev_ts > (self->clock[prev_thrid]))) {
              handle_race(begin + (shadow - get_shadow(begin)), real_sz,
                          0, (state & STATE_LOAD_MASK) != 0);
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
            uint64_t const new_state = (state & ~STATE_TIMESTAMP_MASK)
              | my_ts;
            atomic_uint64_store(shadow, new_state, memory_order_relaxed);
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
  assert(begin != 0 && begin <= end);
  DBG("checking region store %p-%p", begin, end);
  rl_rt_thread_t* self = &thr;
  uint64_t const my_ts = self->clock[self->id];
  uint64_t const state_templ = ((uint64_t)self->id << STATE_THRID_SHIFT)
      | my_ts;
  atomic_uint64_t* shadow = get_shadow(begin);
  atomic_uint64_t* shadow_end = get_shadow(end);
  assert(shadow <= shadow_end);
  for (; shadow != shadow_end; shadow += 1) {
    uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
    // ensure that the address was not used as a sync variable
    if (LIKELY((state & STATE_SYNC_MASK) == 0)) {
      // check that the address is occupied
      size_t const real_sz = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
      if (real_sz != SZ_8 || ((uintptr_t)shadow % 64) == 0) {
        size_t prev_thrid = (state & STATE_THRID_MASK) >> STATE_THRID_SHIFT;
        // ensure that the previous access was from another thread
        if (UNLIKELY(prev_thrid != self->id)) {
          timestamp_t prev_ts = (state & STATE_TIMESTAMP_MASK);
          // check for a race:
          // the previous access should happen before current store
          if (UNLIKELY(prev_ts > (self->clock[prev_thrid])))
            handle_race(begin + (shadow - get_shadow(begin)), real_sz,
                        0, (state & STATE_LOAD_MASK) != 0);
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


void            handle_sync_create   (addr_t addr) {
  rl_rt_sync_t* sync = malloc(sizeof(rl_rt_sync_t));
  memset(sync, 0, sizeof(rl_rt_sync_t));
  //!!! assert(sync);
  assert(((uint64_t)sync & STATE_SYNC_MASK) == 0);
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t new_state = STATE_SYNC_MASK | (uint64_t)sync;
  atomic_uint64_store(shadow, new_state, memory_order_relaxed);
}


void                    handle_sync_destroy (addr_t addr) {
  (void)addr;
}


void            handle_sync_acquire  (addr_t addr, int is_mtx) {
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("acquire at %p, state=%llx", addr, (unsigned long long)state);
  assert(state & STATE_SYNC_MASK);
  rl_rt_sync_t* sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  clock_assign_max(thr.clock, sync->clock);
  dump_clock("thread", thr.clock);
}


void            handle_sync_release  (addr_t addr, int is_mtx) {
  atomic_uint64_t* shadow = get_shadow(addr);
  uint64_t const state = atomic_uint64_load(shadow, memory_order_relaxed);
  DBG("release at %p, state=%llx", addr, (unsigned long long)state);
  assert(state & STATE_SYNC_MASK);
  rl_rt_sync_t* sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  thr.clock[thr.id] += 1;
  clock_assign_max(sync->clock, thr.clock);
  dump_clock("mutex", sync->clock);
}


void relite_enter(void const volatile* p)
{

  printf("enter %p\n", p);
}

void relite_leave()
{
  printf("leave\n");
}



/*
else {
  rl_rt_sync_t* sync = (rl_rt_sync_t*)(state & ~STATE_SYNC_MASK);
  handle_acquire
  (void)sync;
}
*/



/*
#define SHADOW_BASE             ((char*)0x00000D0000000000ull)
#define SHADOW_SIZE             (0x0000800000000000ull - 0x00000E38E38E3800ull)


static uint64_t* get_shadow(void* addr) {
uintptr_t const offset = (uintptr_t)addr
  - (SHADOW_SIZE & ((uintptr_t)(addr < SHADOW_BASE) - 1));
uint64_t* shadow = (uint64_t*)SHADOW_BASE + offset;
return shadow;
}



int main()
{
printf("SHADOW_BASE:      %p\n", SHADOW_BASE);
printf("SHADOW_END:       %p\n", (SHADOW_BASE + SHADOW_SIZE));

printf("%p->%p-%p\n", (void*)0, get_shadow(0), get_shadow(0) + 1);

void* p1 = SHADOW_BASE - 1;
printf("%p->%p-%p\n", p1, get_shadow(p1), get_shadow(p1) + 1);

void* p2 = (SHADOW_BASE + SHADOW_SIZE);
printf("%p->%p-%p\n", p2, get_shadow(p2), get_shadow(p2) + 1);

void* p3 = (void*)0x00007fffffffffffull;
printf("%p->%p-%p\n", p3, get_shadow(p3), get_shadow(p3) + 1);
}
*/


//  #if defined(__GNUC__) && __WORDSIZE == 64



int RunningOnValgrind()
{
  return 0;
}






