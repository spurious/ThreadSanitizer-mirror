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

//TODO(dvyukov): wrap pthread_yield()
//TODO(dvyukov): wrap usleep()
//TODO(dvyukov): sem_getvalue()
//TODO(dvyukov): wrap nanosleep()
//TODO(dvyukov): sem can be used in "inverse" mode


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


void tsan_rtl_sched_shake(bool heavy) {
  unsigned rnd;
  if (heavy == false)
    rnd = tsan_rtl_rand() % 100;
  else
    rnd = 95 + tsan_rtl_rand() % 5;

  if (rnd < 90) {
    // no delay
  } else if (rnd < 95) {
    // rnd = [90..94] -> active delay
    for (unsigned i = 0; i != (rnd - 90 + 1) * 1000; i += 1) {
      __asm__ __volatile__ ("pause");
    }
  } else if (rnd < 98) {
    // rnd = [95..97] -> passive yield
    for (unsigned i = 0; i != (rnd - 95 + 1); i += 1) {
      pthread_yield();
    }
  } else /* if (rnd < 100) */ {
    // rnd = [98..99] -> passive delay
    for (unsigned i = 0; i != (rnd - 98 + 1); i += 1) {
      usleep(10 * 1000);
    }
  }
}


