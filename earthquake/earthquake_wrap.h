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
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif


void  eq_init                     (int do_sched_shake,
                                   int do_api_ambush,
                                   void* (*malloc)(size_t),
                                   void (*free)(void*),
                                   int (*yield)(),
                                   int (*usleep)(unsigned));


int   eq_sem_wait                 (void* func,
                                   sem_t* sem);
int   eq_sem_timedwait            (void* func,
                                   sem_t* sem,
                                   struct timespec const* abs_timeout);
int   eq_sem_trywait              (void* func,
                                   sem_t* sem);
int   eq_sem_post                 (void* func,
                                   sem_t* sem);
int   eq_sem_getvalue             (void* func,
                                   sem_t* sem,
                                   int* value);

int   eq_pthread_mutex_lock       (void* func,
                                   pthread_mutex_t* mtx);
int   eq_pthread_mutex_trylock    (void* func,
                                   pthread_mutex_t* mtx);
int   eq_pthread_mutex_unlock     (void* func,
                                   pthread_mutex_t* mtx);

int   eq_pthread_cond_init        (void* func,
                                   pthread_cond_t* cv,
                                   pthread_condattr_t const* attr);
int   eq_pthread_cond_destroy     (void* func,
                                   pthread_cond_t* cv);
int   eq_pthread_cond_signal      (void* func,
                                   pthread_cond_t* cv);
int   eq_pthread_cond_broadcast   (void* func,
                                   pthread_cond_t* cv);
int   eq_pthread_cond_wait        (void* func,
                                   pthread_cond_t* cv,
                                   pthread_mutex_t* mtx);
int   eq_pthread_cond_timedwait   (void* func,
                                   pthread_cond_t* cv,
                                   pthread_mutex_t* mtx,
                                   struct timespec const* abstime);

int   eq_pthread_rwlock_trywrlock (void* func,
                                   pthread_rwlock_t* mtx);
int   eq_pthread_rwlock_wrlock    (void* func,
                                   pthread_rwlock_t* mtx);
int   eq_pthread_rwlock_tryrdlock (void* func,
                                   pthread_rwlock_t* mtx);
int   eq_pthread_rwlock_rdlock    (void* func,
                                   pthread_rwlock_t* mtx);
int   eq_pthread_rwlock_unlock    (void* func,
                                   pthread_rwlock_t* mtx);

int   eq_pthread_spin_lock        (void* func,
                                   pthread_spinlock_t* mtx);
int   eq_pthread_spin_trylock     (void* func,
                                   pthread_spinlock_t* mtx);
int   eq_pthread_spin_unlock      (void* func,
                                   pthread_spinlock_t* mtx);

int   eq_pthread_create           (void* func,
                                   pthread_t* thr,
                                   pthread_attr_t const* attr,
                                   void* (*start_routine)(void*),
                                   void* arg);

int   eq_usleep                   (void* func,
                                   useconds_t usec);
int   eq_nanosleep                (void* func,
                                   struct timespec const* req,
                                   struct timespec* rem);
unsigned int  eq_sleep            (void* func,
                                   unsigned int seconds);
int   eq_clock_nanosleep          (void* func,
                                   clockid_t clock_id,
                                   int flags,
                                   struct timespec const* request,
                                   struct timespec* remain);
int   eq_sched_yield              (void* func,
                                   int);
int   eq_pthread_yield            (void* func,
                                   int);

int   eq_epoll_wait               (void* func,
                                   int epfd,
                                   struct epoll_event* events,
                                   int maxevents,
                                   int timeout);

void  eq_shake                    (void* func,
                                   int epfd,
                                   struct epoll_event* events,
                                   int maxevents,
                                   int timeout);

#ifdef __cplusplus
}
#endif



