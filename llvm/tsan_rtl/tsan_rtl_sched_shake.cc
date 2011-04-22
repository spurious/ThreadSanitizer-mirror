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

#include "tsan_rtl_sched_shake.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

//TODO(dvyukov): sem can be used in "inverse" mode
//TODO(dvyukov): wrap atomics
//TODO(dvyukov): improve mm modelling precision


enum shake_strength_e {
  strength_none,
  strength_normal,
  strength_above_normal,
  strength_highest,
};


struct hist_event_t {
  shake_event_e         ev;
  uintptr_t             ctx;
};


size_t const hist_size = 4;
static __thread hist_event_t hist [hist_size];
static __thread size_t hist_pos;


unsigned tsan_rtl_rand() {
  static __thread unsigned state = 0;
  if (state == 0) {
    state = ((unsigned)pthread_self() + (unsigned)time(0)) * 31 + 11;
  }
  unsigned rnd = state * 1103515245 + 12345;
  state = rnd;
  rnd = rnd << 16;
  return rnd;
}


static void shake_delay_impl(unsigned const nop_probability,
                             unsigned const active_spin_probability,
                             unsigned const passive_spin_probability,
                             unsigned const sleep_probability) {
  unsigned const total = nop_probability
                       + active_spin_probability
                       + passive_spin_probability
                       + sleep_probability;
  int rnd = tsan_rtl_rand() % total;
  if ((rnd -= nop_probability) < 0) {
    // no delay
  }
  else if ((rnd -= active_spin_probability) < 0) {
    for (int i = 0; i != -rnd; i += 1) {
      for (int j = 0; j != 1000; j += 1) {
        __asm__ __volatile__ ("pause");
      }
    }
  }
  else if ((rnd -= passive_spin_probability) < 0) {
    for (int i = 0; i != -rnd; i += 1) {
      __real_pthread_yield();
    }
  }
  else if ((rnd -= sleep_probability) < 0) {
    for (int i = 0; i != -rnd; i += 1) {
      __real_usleep(10 * 1000); // 10ms
    }
  } else {
    assert(!"invalid delay probability calculation");
  }
}


static void shake_delay(shake_strength_e const strength) {
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


static bool is_atomic(shake_event_e const ev) {
  return ev & (shake_atomic_load | shake_atomic_store | shake_atomic_rmw);
}


static bool is_mutex_lock(shake_event_e const ev) {
  return ev & (shake_mutex_lock | shake_mutex_trylock
      | shake_mutex_rdlock | shake_mutex_tryrdlock);
}


static shake_strength_e calculate_strength(shake_event_e const ev,
                                           uintptr_t const ctx) {
  hist_event_t const& prev = hist[(hist_pos - 1) % hist_size];

  if (is_atomic(ev)) {
    if (prev.ctx == ctx)
      // There are series of atomic operations,
      // so do more shakes to stress it.
      return strength_highest;
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
    if (prev.ev == shake_cond_wait || prev.ev == shake_cond_timedwait)
      // We've just done shake in cond wait
      return strength_none;
    else if (prev.ctx == ctx)
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


void tsan_rtl_shake(shake_event_e const ev,
                    uintptr_t const ctx) {
  assert(ev != shake_none);
  shake_strength_e strength = calculate_strength(ev, ctx);
  shake_delay(strength);

  hist_event_t& hev = hist[hist_pos % hist_size];
  hev.ev = ev;
  hev.ctx = ctx;
  hist_pos += 1;
}




