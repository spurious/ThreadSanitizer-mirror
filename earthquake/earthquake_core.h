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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


enum shake_event_e {
  shake_none            = 0, // used in static initialization
  shake_thread_create   = 1 << 0,
  shake_thread_start    = 1 << 1,
  shake_sem_wait        = 1 << 2,
  shake_sem_trywait     = 1 << 3,
  shake_sem_timedwait   = 1 << 4,
  shake_sem_post        = 1 << 5,
  shake_sem_getvalue    = 1 << 6,
  shake_mutex_lock      = 1 << 7,
  shake_mutex_trylock   = 1 << 8,
  shake_mutex_rdlock    = 1 << 9,
  shake_mutex_tryrdlock = 1 << 10,
  shake_mutex_unlock    = 1 << 11,
  shake_cond_signal     = 1 << 12,
  shake_cond_broadcast  = 1 << 13,
  shake_cond_wait       = 1 << 14,
  shake_cond_timedwait  = 1 << 15,
  shake_atomic_load     = 1 << 16,
  shake_atomic_store    = 1 << 17,
  shake_atomic_rmw      = 1 << 18,
  shake_atomic_fence    = 1 << 19,
};


extern int              eq_do_sched_shake;
extern int              eq_do_api_ambush;


unsigned                eq_rand               ();
void                    eq_sched_shake_impl   (enum shake_event_e ev,
                                               void* ctx);


static __inline void    eq_sched_shake        (enum shake_event_e ev,
                                               void const volatile* ctx) {
  if (eq_do_sched_shake)
    eq_sched_shake_impl(ev, (void*)ctx);
}


#ifdef __cplusplus
}
#endif



