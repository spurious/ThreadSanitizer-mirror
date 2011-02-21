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
#include <errno.h>
#include <malloc.h>


typedef struct relite_pthread_ctx_t {
  void*         (*start_routine)(void*);
  void*         arg;
  void*         ret;
} relite_pthread_ctx_t;


static void* thread_wrapper(void* p) {
  relite_pthread_ctx_t* ctx = (relite_pthread_ctx_t*)p;
  handle_thread_start();
  handle_sync_acquire(ctx, 0);
  ctx->ret = ctx->start_routine(ctx->arg);
  handle_sync_release(ctx, 0);
  handle_thread_end();
  return ctx;
}


int     relite_pthread_create    (pthread_t* th,
                                   pthread_attr_t* attr,
                                   void *(*start_routine)(void*),
                                   void *arg) {
  DBG("intercepting pthread_create");
  relite_pthread_ctx_t* ctx = malloc(sizeof(relite_pthread_ctx_t));
  if (ctx == 0)
    return ENOMEM;
  ctx->start_routine = start_routine;
  ctx->arg = arg;
  //!!! handle DETACHED
  handle_sync_create(ctx);
  handle_sync_release(ctx, 0);
  return pthread_create(th, attr, thread_wrapper, ctx);
}


int     relite_pthread_join      (pthread_t th, void** retval) {
  DBG("intercepting pthread_join");
  relite_pthread_ctx_t* ctx;
  int res = pthread_join(th, (void**)&ctx);
  handle_sync_acquire(ctx, 0);
  if (retval)
    *retval = ctx->ret;
  free(ctx);
  return res;
}


int     relite_pthread_mutex_init    (pthread_mutex_t* mtx,
                                       pthread_mutexattr_t* attr) {
  DBG("intercepting pthread_mutex_init");
  handle_sync_create(mtx);
  return pthread_mutex_init(mtx, attr);
}


int     relite_pthread_mutex_destroy (pthread_mutex_t* mtx) {
  DBG("intercepting pthread_mutex_destroy");
  int res = pthread_mutex_destroy(mtx);
  handle_sync_destroy(mtx);
  return res;
}


int     relite_pthread_mutex_lock    (pthread_mutex_t* mtx) {
  int res = pthread_mutex_lock(mtx);
  DBG("intercepting pthread_mutex_lock");
  handle_sync_acquire(mtx, 1);
  return res;
}


int     relite_pthread_mutex_unlock  (pthread_mutex_t* mtx) {
  DBG("intercepting pthread_mutex_unlock");
  handle_sync_release(mtx, 1);
  return pthread_mutex_unlock(mtx);
}


int     relite_pthread_cond_init     (pthread_cond_t* cv,
                                       pthread_condattr_t const* attr) {
  int res = pthread_cond_init(cv, attr);
  return res;
}


int     relite_pthread_cond_destroy  (pthread_cond_t* cv) {
  int res = pthread_cond_destroy(cv);
  return res;
}


int     relite_pthread_cond_signal   (pthread_cond_t* cv) {
  int res = pthread_cond_signal(cv);
  return res;
}


int     relite_pthread_cond_broadcast(pthread_cond_t* cv) {
  int res = pthread_cond_broadcast(cv);
  return res;
}


int     relite_pthread_cond_wait     (pthread_cond_t* cv,
                                       pthread_mutex_t* mtx) {
  handle_sync_release(mtx, 1);
  int res = pthread_cond_wait(cv, mtx);
  handle_sync_acquire(mtx, 1);
  return res;
}


int     relite_pthread_cond_timedwait(pthread_cond_t* cv,
                                       pthread_mutex_t* mtx,
                                       struct timespec const* ts) {
  handle_sync_release(mtx, 1);
  int res = pthread_cond_timedwait(cv, mtx, ts);
  handle_sync_acquire(mtx, 1);
  return res;
}




