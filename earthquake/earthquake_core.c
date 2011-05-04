/* Copyright (c) 2011, Google Inc.
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

#include "earthquake_core.h"
#include <stdio.h>
#include <assert.h>


int                     eq_do_sched_shake;
int                     eq_do_api_ambush;
void*                   (*eq_func_malloc)(size_t);
void                    (*eq_func_free)(void*);
int                     (*eq_func_yield)();
int                     (*eq_func_usleep)(unsigned);


enum shake_strength_e {
  strength_none,
  strength_normal,
  strength_above_normal,
  strength_highest,
};


struct hist_event_t {
  enum shake_event_e    ev;
  void*                 ctx;
};


#define HIST_SIZE 4
static __thread struct hist_event_t hist [HIST_SIZE];
static __thread int hist_pos;


static unsigned rdtsc()
{
  unsigned lower;
  unsigned upper;
  __asm__ __volatile__("rdtsc" : "=a"(lower), "=d"(upper));
  return lower;
}


static void processor_yield()
{
  __asm__ __volatile__ ("pause");
}


unsigned eq_rand() {
  static __thread unsigned state = 0;
  if (state == 0) {
    state = rdtsc();
  }
  unsigned rnd = state * 1103515245 + 12345;
  state = rnd;
  rnd = rnd >> 16;
  return rnd;
}


void eq_init(int do_sched_shake,
             int do_api_ambush,
             void* (*malloc)(size_t),
             void (*free)(void*),
             int (*yield)(),
             int (*usleep)(unsigned)) {
  eq_do_sched_shake = do_sched_shake;
  eq_do_api_ambush = do_api_ambush;
  eq_func_malloc = malloc;
  eq_func_free = free;
  eq_func_yield = yield;
  eq_func_usleep = usleep;
  if (do_sched_shake != 0 || do_api_ambush != 0) {
    fprintf(stderr, "EARTHQUAKE ACTIVATED"
        " (sched_shake=%d, api_ambush=%d, malloc=%p, free=%p,"
        " yield=%p, usleep=%p)\n",
        do_sched_shake, do_api_ambush, malloc, free, yield, usleep);
  }
}


void*                   eq_malloc             (size_t sz) {
  return eq_func_malloc(sz);
}


void                    eq_free               (void* p) {
  eq_func_free(p);
}


static void shake_delay_impl(unsigned const nop_probability,
                             unsigned const active_spin_probability,
                             unsigned const passive_spin_probability,
                             unsigned const sleep_probability) {
  unsigned const total = nop_probability
                       + active_spin_probability
                       + passive_spin_probability
                       + sleep_probability;
  int i, j;
  int rnd = eq_rand() % total;
  if ((rnd -= nop_probability) < 0) {
    // no delay
  }
  else if ((rnd -= active_spin_probability) < 0) {
    for (i = 0; i != -rnd; i += 1) {
      for (j = 0; j != 1000; j += 1) {
        processor_yield();
      }
    }
  }
  else if ((rnd -= passive_spin_probability) < 0) {
    for (i = 0; i != -rnd; i += 1) {
      if (eq_func_yield != 0)
        eq_func_yield();
    }
  }
  else if ((rnd -= sleep_probability) < 0) {
    for (i = 0; i != -rnd; i += 1) {
      if (eq_func_usleep != 0)
        eq_func_usleep(10 * 1000); // 10ms
    }
  } else {
    assert(!"invalid delay probability calculation");
  }
}


static void shake_delay(enum shake_strength_e const strength) {
  if (strength == strength_none) {
    // no delay
  } else if (strength == strength_normal) {
    shake_delay_impl(95, 2, 2, 1);
  } else if (strength == strength_above_normal) {
    shake_delay_impl(80, 10, 7, 3);
  } else if (strength == strength_highest) {
    shake_delay_impl(40, 20, 20, 20);
  } else {
    assert(!"invalid delay strength value");
  }
}


static int is_atomic(enum shake_event_e const ev) {
  return ev & (shake_atomic_load | shake_atomic_store | shake_atomic_rmw);
}


static int is_mutex_lock(enum shake_event_e const ev) {
  return ev & (shake_mutex_lock | shake_mutex_trylock
      | shake_mutex_rdlock | shake_mutex_tryrdlock);
}


static enum shake_strength_e calculate_strength(enum shake_event_e const ev,
                                                void* const ctx) {
  struct hist_event_t const* prev = &hist[(hist_pos - 1) % HIST_SIZE];

  if (is_atomic(ev)) {
    if (prev->ctx == ctx)
      // There are series of atomic operations,
      // so do more shakes to stress it.
      return strength_above_normal; //!!! strength_highest;
    else
      return strength_above_normal;

  } else if (ev == shake_mutex_unlock) {
    // No sense to do shake here.
    return strength_none;

  } else if (ev == shake_thread_create
          || ev == shake_thread_start) {
    // We want to check both variants -
    // when a parent thread starts first and when a child thread starts first.
    return strength_highest;

  } else if (is_mutex_lock(ev)) {
    if (prev->ev == shake_cond_wait || prev->ev == shake_cond_timedwait)
      // We've just done shake in cond wait
      return strength_none;
    else if (prev->ctx == ctx)
      // Series of locks on the same mutex,
      // that's suspicious.
      return strength_above_normal;
    else
      return strength_normal;

  } else if (ev == shake_cond_wait
      || ev == shake_cond_timedwait) {
    // Presumably that's a subtle moment.
    return strength_above_normal;
  }

  return strength_normal;
}


void eq_sched_shake_impl(enum shake_event_e const ev,
                         void* const ctx) {
  assert(ev != shake_none);
  enum shake_strength_e strength = calculate_strength(ev, ctx);
  shake_delay(strength);

  struct hist_event_t* hev = &hist[hist_pos % HIST_SIZE];
  hev->ev = ev;
  hev->ctx = ctx;
  hist_pos += 1;
}





