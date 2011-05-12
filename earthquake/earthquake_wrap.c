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

#include "earthquake_wrap.h"
#include "earthquake_core.h"
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>


//TODO(dvyukov): intercept poll/select/recv/recvmsg/send

//TODO(dvyukov): intercept lockf - it's used in some shmem communication

//TODO(dvyukov): mock thread pool so that it creates thread per task


static int64_t timespec_to_int64(const struct timespec* ts) {
  return (int64_t)ts->tv_sec * 1000*1000*1000 + ts->tv_nsec;
}


static struct timespec int64_to_timespec(int64_t t) {
  struct timespec ts = {};
  ts.tv_sec = (time_t)(t / (1000*1000*1000));
  ts.tv_nsec = (long)(t % (1000*1000*1000));
  return ts;
}


int   eq_sem_wait                 (void* func,
                                   sem_t* sem) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = EINTR;
      return -1;
    }
  }
  eq_sched_shake(shake_sem_wait, sem);
  int rv = ((int(*)(sem_t*))func)(sem);
  return rv;
}


int   eq_sem_timedwait            (void* func,
                                   sem_t* sem,
                                   struct timespec const* abs_timeout) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = ETIMEDOUT;
      return -1;
    }
    if (eq_rand() % 2) {
      errno = EINTR;
      return -1;
    }
    struct timespec ts;
    if ((abs_timeout != 0) && (eq_rand() % 2)) {
      abs_timeout = &ts;
      ts.tv_sec = time(0);
      ts.tv_nsec = (eq_rand() % 1000) * 1000*1000;
    }
  }
  eq_sched_shake(shake_sem_timedwait, sem);
  int rv = ((int(*)(sem_t*, struct timespec const*))func)(sem, abs_timeout);
  return rv;
}


int   eq_sem_trywait              (void* func,
                                   sem_t* sem) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = EINTR;
      return -1;
    }
  }
  eq_sched_shake(shake_sem_trywait, sem);
  int rv = ((int(*)(sem_t*))func)(sem);
  return rv;
}


int   eq_sem_post                 (void* func,
                                   sem_t* sem) {
  eq_sched_shake(shake_sem_post, sem);
  int rv = ((int(*)(sem_t*))func)(sem);
  return rv;
}


int   eq_sem_getvalue             (void* func,
                                   sem_t* sem,
                                   int* value) {
  eq_sched_shake(shake_sem_getvalue, sem);
  int rv = ((int(*)(sem_t*, int*))func)(sem, value);
  return rv;
}


int   eq_pthread_mutex_lock       (void* func,
                                   pthread_mutex_t* mtx) {
  eq_sched_shake(shake_mutex_lock, mtx);
  int rv = ((int(*)(pthread_mutex_t*))func)(mtx);
  return rv;
}


int   eq_pthread_mutex_trylock    (void* func,
                                   pthread_mutex_t* mtx) {
  if (eq_do_api_ambush) {
    // Strictly saying that's incorrect according to POSIX
    // (trylock() on an unlocked mutex must not return EBUSY),
    // but it's a shame for a sane program to rely on that anyway :)
    if (eq_rand() % 2) {
      return EBUSY;
    }
  }
  eq_sched_shake(shake_mutex_trylock, mtx);
  int rv = ((int(*)(pthread_mutex_t*))func)(mtx);
  return rv;
}


int   eq_pthread_mutex_unlock     (void* func,
                                   pthread_mutex_t* mtx) {
  eq_sched_shake(shake_mutex_unlock, mtx);
  int rv = ((int(*)(pthread_mutex_t*))func)(mtx);
  return rv;
}


int   eq_pthread_cond_init        (void* func,
                                   pthread_cond_t* cv,
                                   pthread_condattr_t const* attr) {
  int rv = ((int(*)(pthread_cond_t*, pthread_condattr_t const*))func)(cv, attr);
  return rv;
}


int   eq_pthread_cond_destroy     (void* func,
                                   pthread_cond_t* cv) {
  int rv = ((int(*)(pthread_cond_t*))func)(cv);
  return rv;
}


int   eq_pthread_cond_signal      (void* func,
                                   pthread_cond_t* cv) {
  eq_sched_shake(shake_cond_signal, cv);
  int rv = ((int(*)(pthread_cond_t*))func)(cv);
  return rv;
}


int   eq_pthread_cond_broadcast   (void* func,
                                   pthread_cond_t* cv) {
  eq_sched_shake(shake_cond_broadcast, cv);
  int rv = ((int(*)(pthread_cond_t*))func)(cv);
  return rv;
}


int   eq_pthread_cond_wait        (void* func,
                                   pthread_cond_t* cv,
                                   pthread_mutex_t* mtx) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      return EINTR;
    }
  }
  int rv = ((int(*)(pthread_cond_t*, pthread_mutex_t*))func)(cv, mtx);
  //!!!
//  if (G_flags->sched_shake) {
//    eq_pthread_mutex_unlock(mutex);
//    eq_sched_shake(shake_cond_wait, cv);
//    eq_pthread_mutex_lock(mutex);
//  }
  return rv;
}


int   eq_pthread_cond_timedwait   (void* func,
                                   pthread_cond_t* cv,
                                   pthread_mutex_t* mtx,
                                   struct timespec const* abstime) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      return ETIMEDOUT;
    }
    if (eq_rand() % 2) {
      return EINTR;
    }
    struct timespec ts;
    if ((abstime != 0) && (eq_rand() % 2)) {
      abstime = &ts;
      ts.tv_sec = time(0);
      ts.tv_nsec = (eq_rand() % 1000) * 1000*1000;
    }
  }
  int rv = ((int(*)(pthread_cond_t*, pthread_mutex_t*, struct timespec const*))
      func)(cv, mtx, abstime);
//  if (G_flags->sched_shake) {
//    eq_pthread_mutex_unlock(mutex);
//    eq_sched_shake(shake_cond_timedwait, cv);
//    eq_pthread_mutex_lock(mutex);
//  }
  return rv;
}


int   eq_pthread_rwlock_trywrlock (void* func,
                                   pthread_rwlock_t* mtx) {
  eq_sched_shake(shake_mutex_trylock, mtx);
  int rv = ((int(*)(pthread_rwlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_rwlock_wrlock    (void* func,
                                   pthread_rwlock_t* mtx) {
  eq_sched_shake(shake_mutex_lock, mtx);
  int rv = ((int(*)(pthread_rwlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_rwlock_tryrdlock (void* func,
                                   pthread_rwlock_t* mtx) {
  eq_sched_shake(shake_mutex_tryrdlock, mtx);
  int rv = ((int(*)(pthread_rwlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_rwlock_rdlock    (void* func,
                                   pthread_rwlock_t* mtx) {
  eq_sched_shake(shake_mutex_rdlock, mtx);
  int rv = ((int(*)(pthread_rwlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_rwlock_unlock    (void* func,
                                   pthread_rwlock_t* mtx) {
  eq_sched_shake(shake_mutex_unlock, mtx);
  int rv = ((int(*)(pthread_rwlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_spin_lock        (void* func,
                                   pthread_spinlock_t* mtx) {
  eq_sched_shake(shake_mutex_lock, mtx);
  int rv = ((int(*)(pthread_spinlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_spin_trylock     (void* func,
                                   pthread_spinlock_t* mtx) {
  if (eq_do_api_ambush) {
    // Strictly saying that's incorrect according to POSIX
    // (trylock() on an unlocked mutex must not return EBUSY),
    // but it's a shame for a sane program to rely on that anyway :)
    if (eq_rand() % 2) {
      return EBUSY;
    }
  }
  eq_sched_shake(shake_mutex_trylock, mtx);
  int rv = ((int(*)(pthread_spinlock_t*))func)(mtx);
  return rv;
}


int   eq_pthread_spin_unlock      (void* func,
                                   pthread_spinlock_t* mtx) {
  eq_sched_shake(shake_mutex_unlock, mtx);
  int rv = ((int(*)(pthread_spinlock_t*))func)(mtx);
  return rv;
}


struct thread_arg_t {
  void* (*start_routine)(void*);
  void* arg;
  int do_delay_create;
};


static void* thread_thunk(void* arg) {
  struct thread_arg_t* ctx = (struct thread_arg_t*)arg;
  if (ctx->do_delay_create == 0) {
    eq_sched_shake(shake_thread_start, (void*)ctx->start_routine);
  }
  void* rv = ctx->start_routine(ctx->arg);
  eq_free(ctx);
  return rv;
}


int   eq_pthread_create           (void* func,
                                   pthread_t* thr,
                                   pthread_attr_t const* attr,
                                   void* (*start_routine)(void*),
                                   void* arg) {
  int do_delay_create = (eq_rand() % 2);
  struct thread_arg_t* ctx = (struct thread_arg_t*)
      eq_malloc(sizeof(struct thread_arg_t));
  ctx->start_routine = start_routine;
  ctx->arg = arg;
  ctx->do_delay_create = do_delay_create;
  int rv = ((int(*)(pthread_t*, pthread_attr_t const*, void*(*)(void*), void*))
      func)(thr, attr, thread_thunk, ctx);
  if (do_delay_create != 0) {
    eq_sched_shake(shake_thread_create, (void*)start_routine);
  }
  return rv;
}


int   eq_usleep                   (void* func,
                                   useconds_t usec) {
  int reserve = 0;
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = EINTR;
      return -1;
    }
    if (eq_rand() % 2) {
      reserve = eq_rand() % (usec + 1000) - 1000;
      usec -= reserve;
    }
  }
  int rv = ((int(*)(useconds_t))func)(usec);
  if (rv == 0 && reserve > 0) {
    errno = EINTR;
    return -1;
  }
  return rv;  
}


int   eq_nanosleep                (void* func,
                                   struct timespec const* req,
                                   struct timespec* rem) {
  if (req == 0) {
    errno = EINVAL;
    return -1;
  }
  struct timespec req2 = *req;
  int64_t reserve = 0;
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = EINTR;
      if (rem != 0)
        *rem = *req;
      return -1;
    }
    if (eq_rand() % 2) {
      int64_t wait = timespec_to_int64(req);
      reserve = (((int64_t)eq_rand() % (wait / (100*1000) + 1000)) - 1000)
          * 100*1000;
      wait -= reserve;
      req2 = int64_to_timespec(wait);
    }
  }

  int rv = ((int(*)(struct timespec const* req, struct timespec*))
      func)(&req2, rem);

  if (reserve != 0) {
    int64_t remain = 0;
    if (rv != 0 && rem != 0)
      remain = timespec_to_int64(rem);
    remain += reserve;
    if (remain > 0) {
      if (rem != 0) {
        *rem = int64_to_timespec(remain);
        if (timespec_to_int64(rem) > timespec_to_int64(req))
          *rem = *req;
      }
      errno = EINTR;
      rv = -1;
    } else {
      rv = 0;
    }
  }
  return rv;
}


unsigned int  eq_sleep            (void* func,
                                   unsigned int seconds) {
  int reserve = 0;
  if (eq_do_api_ambush) {
    if (seconds == 0 || eq_rand() % 2)
      return seconds;
    reserve = eq_rand() % seconds;
    seconds -= reserve;
  }
  unsigned int rv = ((int(*)(unsigned int))func)(seconds);
  rv += reserve;
  return rv;
}


int   eq_clock_nanosleep          (void* func,
                                   clockid_t clock_id,
                                   int flags,
                                   struct timespec const* request,
                                   struct timespec* remain) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      if (request != 0 && remain != 0)
        *remain = *request;
      errno = EINTR;
      return -1;
    }
  }
  int rv = ((int(*)(clockid_t, int, struct timespec const*, struct timespec*))
      func)(clock_id, flags, request, remain);
  return rv;
}


int   eq_sched_yield              (void* func,
                                   int unused) {
  (void)unused;
  if (eq_do_api_ambush) {
    if (eq_rand() % 2)
      return 0;
  }
  int rv = ((int(*)(void))func)();
  return rv;
}


int   eq_pthread_yield            (void* func,
                                   int unused) {
  return eq_sched_yield(func, unused);
}


int   eq_epoll_wait               (void* func,
                                   int epfd,
                                   struct epoll_event* events,
                                   int maxevents,
                                   int timeout) {
  if (eq_do_api_ambush) {
    if (eq_rand() % 2) {
      errno = EINTR;
      return -1;
    }
    if (eq_rand() % 2)
      timeout = eq_rand() % (timeout + 100);
  }
  int rv = ((int(*)(int, struct epoll_event*, int, int))
      func)(epfd, events, maxevents, timeout);
  return rv;
}




