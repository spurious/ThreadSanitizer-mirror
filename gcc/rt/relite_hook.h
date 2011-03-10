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

#ifndef RELITE_HOOK_H_INCLUDED
#define RELITE_HOOK_H_INCLUDED

#include <stddef.h>
#include <assert.h>


typedef enum relite_hook_e {
  relite_hook_malloc,
  relite_hook_calloc,
  relite_hook_realloc,
  relite_hook_free,

  relite_hook_memset,
  relite_hook_memcpy,
  relite_hook_memcmp,

  relite_hook_pthread_create,
  relite_hook_pthread_join,

  relite_hook_pthread_mutex_init,
  relite_hook_pthread_mutex_destroy,
  relite_hook_pthread_mutex_lock,
  relite_hook_pthread_mutex_trylock,
  relite_hook_pthread_mutex_unlock,

  relite_hook_pthread_cond_init,
  relite_hook_pthread_cond_destroy,
  relite_hook_pthread_cond_signal,
  relite_hook_pthread_cond_broadcast,
  relite_hook_pthread_cond_wait,
  relite_hook_pthread_cond_timedwait,
} relite_hook_e;


typedef void            (*relite_hook_f)    ();


void                    relite_hook_init    ();
relite_hook_f           relite_hook_get     (relite_hook_e hook);


void*                   relite_malloc       (size_t size);
void                    relite_free         (void* ptr);


#endif

