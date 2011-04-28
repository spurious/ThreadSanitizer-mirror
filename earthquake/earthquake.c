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

#define _GNU_SOURCE
#include <dlfcn.h>
#include "earthquake_wrap.h"


#define EQ_CAT(A, B) EQ_CAT2(A, B)
#define EQ_CAT2(A, B) A##B

#define EQ_FORWARD(name, ...) \
  static void* original = 0; \
  if (original == 0) \
    original = dlsym(RTLD_NEXT, #name); \
  return EQ_CAT(eq_,name)(original, __VA_ARGS__); \
/**/


void constructor() __attribute__((constructor));
void constructor() {
  eq_init(1, 1,
          dlsym(RTLD_NEXT, "sched_yield"),
          dlsym(RTLD_NEXT, "usleep"));
}


int sem_wait(sem_t* sem) {
  EQ_FORWARD(sem_wait, sem);
}

int sem_timedwait(sem_t* sem, struct timespec const* abs_timeout) {
  EQ_FORWARD(sem_timedwait, sem, abs_timeout);
}

int sem_trywait(sem_t* sem) {
  EQ_FORWARD(sem_trywait, sem);
}

int sem_post(sem_t* sem) {
  EQ_FORWARD(sem_post, sem);
}

int sem_getvalue(sem_t* sem, int* value) {
  EQ_FORWARD(sem_getvalue, sem, value);
}

int pthread_mutex_lock(pthread_mutex_t* mtx) {
  EQ_FORWARD(pthread_mutex_lock, mtx);
}

int pthread_mutex_trylock(pthread_mutex_t* mtx) {
  EQ_FORWARD(pthread_mutex_trylock, mtx);
}

int pthread_mutex_unlock(pthread_mutex_t* mtx) {
  EQ_FORWARD(pthread_mutex_unlock, mtx);
}

int pthread_cond_init(pthread_cond_t* cv, pthread_condattr_t const* attr) {
  EQ_FORWARD(pthread_cond_init, cv, attr);
}

 int pthread_cond_destroy(pthread_cond_t* cv) {
   EQ_FORWARD(pthread_cond_destroy, cv);
 }

int pthread_cond_signal(pthread_cond_t* cv) {
  EQ_FORWARD(pthread_cond_signal, cv);
}

int pthread_cond_broadcast(pthread_cond_t* cv) {
  EQ_FORWARD(pthread_cond_broadcast, cv);
}

int pthread_cond_wait(pthread_cond_t* cv, pthread_mutex_t* mtx) {
  EQ_FORWARD(pthread_cond_wait, cv, mtx);
}

int pthread_cond_timedwait(pthread_cond_t* cv, pthread_mutex_t* mtx,
                           struct timespec const* abstime) {
  EQ_FORWARD(pthread_cond_timedwait, cv, mtx, abstime);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* mtx) {
  EQ_FORWARD(pthread_rwlock_trywrlock, mtx);
}

int pthread_rwlock_wrlock(pthread_rwlock_t* mtx) {
  EQ_FORWARD(pthread_rwlock_wrlock, mtx);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* mtx) {
  EQ_FORWARD(pthread_rwlock_tryrdlock, mtx);
}

int pthread_rwlock_rdlock(pthread_rwlock_t* mtx) {
  EQ_FORWARD(pthread_rwlock_rdlock, mtx);
}

int pthread_rwlock_unlock(pthread_rwlock_t* mtx) {
  EQ_FORWARD(pthread_rwlock_unlock, mtx);
}

int pthread_spin_lock(pthread_spinlock_t* mtx) {
  EQ_FORWARD(pthread_spin_lock, mtx);
}

int pthread_spin_trylock(pthread_spinlock_t* mtx) {
  EQ_FORWARD(pthread_spin_trylock, mtx);
}

int pthread_spin_unlock(pthread_spinlock_t* mtx) {
  EQ_FORWARD(pthread_spin_unlock, mtx);
}

int pthread_create(pthread_t* __restrict thr,
                   pthread_attr_t const* __restrict attr,
                   void* (*start_routine)(void*),
                   void* __restrict arg) {
  EQ_FORWARD(pthread_create, thr, attr, start_routine, arg);
}

int usleep(useconds_t usec) {
  EQ_FORWARD(usleep, usec);
}

int nanosleep(struct timespec const* req, struct timespec* rem) {
  EQ_FORWARD(nanosleep, req, rem);
}

unsigned int sleep(unsigned int seconds) {
  EQ_FORWARD(sleep, seconds);
}

int clock_nanosleep(clockid_t clock_id,
                    int flags,
                    struct timespec const* request,
                    struct timespec* remain) {
  EQ_FORWARD(clock_nanosleep, clock_id, flags, request, remain);
}

int sched_yield() {
  EQ_FORWARD(sched_yield, 0);
}

int pthread_yield() {
  EQ_FORWARD(pthread_yield, 0);
}

int epoll_wait(int epfd,
               struct epoll_event* events, 
               int maxevents,
               int timeout) {
  EQ_FORWARD(epoll_wait, epfd, events, maxevents, timeout);
}




