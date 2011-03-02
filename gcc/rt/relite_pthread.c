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

#include "relite_rt_int.h"
#include "relite_pthread.h"
#include "relite_dbg.h"
#include "relite_hook.h"
#include <pthread.h>
#include <errno.h>
#include <malloc.h>


#define GENERATE_HOOK(name, ret_type, ...) \
  (ret_type (*real_##name##_f)(__VA_ARGS__))relite_hook_get(relite_hook_##name);

//TODO(dvyukov): intercept sem and futex APIs


typedef struct relite_pthread_ctx_t {
  void*         (*start_routine)(void*);
  void*         arg;
  void*         ret;
} relite_pthread_ctx_t;


static void* relite_thread_wrapper(void* p) {
  relite_pthread_ctx_t* ctx = (relite_pthread_ctx_t*)p;
  handle_thread_start();
  handle_sync_acquire(ctx, 0);
  ctx->ret = ctx->start_routine(ctx->arg);
  handle_sync_release(ctx, 0);
  handle_thread_end();
  return ctx;
}


int                     pthread_create      (pthread_t* th,
                                             pthread_attr_t const* attr,
                                             void* (*start_routine)(void*),
                                             void* arg) {
  DBG("intercepting pthread_create");
  relite_pthread_ctx_t* ctx = relite_malloc(sizeof(relite_pthread_ctx_t));
  if (ctx == 0)
    return ENOMEM;
  ctx->start_routine = start_routine;
  ctx->arg = arg;
  //TODO(dvyukov): handle DETACHED
  handle_sync_create(ctx);
  handle_sync_release(ctx, 0);
  typedef int (*create_f)(pthread_t* th,
                          pthread_attr_t const* attr,
                          void* (*start_routine)(void*),
                          void* arg);
  create_f real = (create_f)relite_hook_get(relite_hook_pthread_create);
  int res = real(th, attr, relite_thread_wrapper, ctx);
  relite_sched_shake();
  return res;
}


int                     pthread_join        (pthread_t th, void** retval) {
  typedef int           (*real_f)           (pthread_t th, void** retval);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_join);
  DBG("intercepting pthread_join");
  relite_pthread_ctx_t* ctx;
  int res = real(th, (void**)&ctx);
  handle_sync_acquire(ctx, 0);
  if (retval)
    *retval = ctx->ret;
  relite_free(ctx);
  return res;
}


int                     pthread_mutex_init  (pthread_mutex_t* mtx,
                                             pthread_mutexattr_t const* attr) {
  typedef int           (*real_f)           (pthread_mutex_t* mtx,
                                             pthread_mutexattr_t const* attr);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_mutex_init);
  DBG("intercepting pthread_mutex_init");
  handle_sync_create(mtx);
  int res = real(mtx, attr);
  return res;
}


int                     pthread_mutex_destroy (pthread_mutex_t* mtx) {
  typedef int           (*real_f)           (pthread_mutex_t* mtx);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_mutex_destroy);
  DBG("intercepting pthread_mutex_destroy");
  int res = real(mtx);
  handle_sync_destroy(mtx);
  return res;
}


int                     pthread_mutex_lock  (pthread_mutex_t* mtx) {
  typedef int           (*real_f)           (pthread_mutex_t* mtx);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_mutex_lock);
  relite_sched_shake();
  int res = real(mtx);
  DBG("intercepting pthread_mutex_lock");
  handle_sync_acquire(mtx, 1);
  return res;
}


int                     pthread_mutex_trylock  (pthread_mutex_t* mtx) {
  typedef int           (*real_f)           (pthread_mutex_t* mtx);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_mutex_trylock);
  DBG("intercepting pthread_mutex_trylock");
  relite_sched_shake();
  int res = real(mtx);
  if (res == 0)
    handle_sync_acquire(mtx, 1);
  return res;
}


int                     pthread_mutex_unlock(pthread_mutex_t* mtx) {
  typedef int           (*real_f)           (pthread_mutex_t* mtx);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_mutex_unlock);
  DBG("intercepting pthread_mutex_unlock");
  handle_sync_release(mtx, 1);
  int res = real(mtx);
  return res;
}


int                     pthread_cond_init   (pthread_cond_t* cv,
                                             pthread_condattr_t const* attr) {
  typedef int           (*real_f)           (pthread_cond_t* cv,
                                             pthread_condattr_t const* attr);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_init);
  int res = real(cv, attr);
  return res;
}


int                     pthread_cond_destroy(pthread_cond_t* cv) {
  typedef int           (*real_f)           (pthread_cond_t* cv);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_destroy);
  int res = real(cv);
  return res;
}


int                     pthread_cond_signal (pthread_cond_t* cv) {
  typedef int           (*real_f)           (pthread_cond_t* cv);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_signal);
  int res = real(cv);
  return res;
}


int                     pthread_cond_broadcast  (pthread_cond_t* cv) {
  typedef int           (*real_f)               (pthread_cond_t* cv);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_broadcast);
  int res = real(cv);
  return res;
}


int                     pthread_cond_wait   (pthread_cond_t* cv,
                                             pthread_mutex_t* mtx) {
#ifdef RELITE_API_AMBUSH
  if (relite_rand(2) == 0)
    return EINTR;
#endif

  typedef int           (*real_f)               (pthread_cond_t* cv,
                                                 pthread_mutex_t* mtx);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_wait);
  handle_sync_release(mtx, 1);
  int res = real(cv, mtx);
  handle_sync_acquire(mtx, 1);
  return res;
}


int                     pthread_cond_timedwait  (pthread_cond_t* cv,
                                                 pthread_mutex_t* mtx,
                                                 struct timespec const* ts) {
#ifdef RELITE_API_AMBUSH
  if (relite_rand(2) == 0)
    return EINTR;
#endif

  typedef int           (*real_f)               (pthread_cond_t* cv,
                                                 pthread_mutex_t* mtx,
                                                 struct timespec const* ts);
  real_f real = (real_f)relite_hook_get(relite_hook_pthread_cond_timedwait);

  handle_sync_release(mtx, 1);
  int res = real(cv, mtx, ts);
  handle_sync_acquire(mtx, 1);
  return res;
}




