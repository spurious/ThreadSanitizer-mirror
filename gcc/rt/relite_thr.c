/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
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

#include "relite_thr.h"
#include "relite_atomic.h"
#include "relite_dbg.h"
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>


#define THREAD_DEFER_COUNT                  16
#define LOCKED                              1
#define UNLOCKED                            0


typedef struct relite_thr_cache_t {
  atomic_uint32_t                           mtx;
  uint32_t                                  total_count;
  uint32_t                                  busy_count;
  relite_thr_t*                             busy_head;
  relite_thr_t*                             busy_tail;
  uint32_t                                  free_count;
  relite_thr_t*                             free_head;
  relite_thr_t*                             free_tail;
} relite_thr_cache_t;


static relite_thr_cache_t                   relite_thr_cache;
__thread relite_thr_t*                      relite_thr_instance;
__thread thrid_t                            relite_dbg_tid;


relite_thr_t*           relite_thr_init     () {
  relite_thr_t* thr;
  relite_thr_cache_t* cache = &relite_thr_cache;
  while (atomic_uint32_exchange
      (&cache->mtx, LOCKED, memory_order_acquire) != UNLOCKED)
    sched_yield();

  if (cache->free_count > THREAD_DEFER_COUNT) {
    assert(cache->free_head != 0);
    assert(cache->free_head->prev != 0);
    assert(cache->free_head->next == 0);
    thr = cache->free_head;
    cache->free_head = cache->free_head->prev;
    cache->free_head->next = 0;
    cache->free_count -= 1;
    int i;
    for (i = 0; i != MAX_THREADS; i += 1) {
      thr->clock[i] = 0;
    }
  } else if (cache->total_count < MAX_THREADS) {
    thr = (relite_thr_t*)mmap(0, sizeof(relite_thr_t),
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (thr == 0)
      relite_fatal("failed to allocate thread descriptor");
    thr->id = cache->total_count++;
    thr->rand = (unsigned)pthread_self() + (unsigned)time(0);
    DBG("thread start %u", thr->id);
  } else {
    relite_fatal("maximum number of threads is reached");
  }
  if (cache->busy_count == 0) {
    assert(cache->busy_head == 0);
    assert(cache->busy_tail == 0);
    thr->next = 0;
    thr->prev = 0;
    cache->busy_head = thr;
    cache->busy_tail = thr;
  } else {
    assert(cache->busy_head != 0);
    assert(cache->busy_tail != 0);
    thr->prev = 0;
    thr->next = cache->busy_tail;
    cache->busy_tail->prev = thr;
    cache->busy_tail = thr;
  }
  cache->busy_count += 1;
  assert(cache->busy_count + cache->free_count == cache->total_count);

  atomic_uint32_store(&cache->mtx, UNLOCKED, memory_order_release);

  assert(thr != 0);
  assert(relite_thr_instance == 0);
  relite_thr_instance = thr;
  relite_dbg_tid = thr->id;
  thr->own_clock += 1;
  thr->clock[thr->id] = thr->own_clock;
  return thr;
}


void                    relite_thr_free     (relite_thr_t* thr) {
  assert(thr != 0);
  relite_thr_cache_t* cache = &relite_thr_cache;
  while (atomic_uint32_exchange
      (&cache->mtx, LOCKED, memory_order_acquire) != UNLOCKED)
    sched_yield();

  if (thr->prev != 0) {
    thr->prev->next = thr->next;
  } else {
    assert(cache->busy_tail == thr);
    cache->busy_tail = thr->next;
  }
  if (thr->next != 0) {
    thr->next->prev = thr->prev;
  } else {
    assert(cache->busy_head == thr);
    cache->busy_head = thr->prev;
  }
  cache->busy_count -= 1;
  if (cache->free_count == 0) {
    assert(cache->free_head == 0);
    assert(cache->free_tail == 0);
    thr->next = 0;
    thr->prev = 0;
    cache->free_head = thr;
    cache->free_tail = thr;
  } else {
    assert(cache->free_head != 0);
    assert(cache->free_tail != 0);
    thr->prev = 0;
    thr->next = cache->free_tail;
    cache->free_tail->prev = thr;
    cache->free_tail = thr;
  }
  cache->free_count += 1;
  assert(cache->busy_count + cache->free_count == cache->total_count);

  atomic_uint32_store(&cache->mtx, UNLOCKED, memory_order_release);
}


unsigned                relite_thr_rand     (relite_thr_t* thr,
                                             unsigned limit) {
  unsigned x = thr->rand;
  thr->rand = x * 1103515245 + 12345;
  return (x >> 16) % limit;
}


