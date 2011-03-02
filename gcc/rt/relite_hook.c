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

#include "relite_hook.h"
#include "relite_dbg.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>


typedef enum relite_lib_e {
  //lib_self,
  lib_libc,
  lib_pthread,
} relite_lib_e;


typedef struct relite_lib_t {
  relite_lib_e          id;
  char const*           name;
  void*                 handle;
} relite_lib_t;


typedef struct relite_hook_t {
  relite_hook_e         id;
  char const*           name;
  relite_lib_e          lib;
  relite_hook_f         func;
} relite_hook_t;


static relite_lib_t libs [] = {
    //{lib_self,                  0},
    {lib_libc,                  "/lib/libc.so.6"},
    {lib_pthread,               "/lib/libpthread-2.11.1.so"},
};


static relite_hook_t hooks [] = {
  {relite_hook_malloc,                  "malloc",                 lib_libc},
  {relite_hook_calloc,                  "calloc",                 lib_libc},
  {relite_hook_realloc,                 "realloc",                lib_libc},
  {relite_hook_free,                    "free",                   lib_libc},

  {relite_hook_memset,                  "memset",                 lib_libc},
  {relite_hook_memcpy,                  "memcpy",                 lib_libc},
  {relite_hook_memcmp,                  "memcmp",                 lib_libc},

  {relite_hook_pthread_create,          "pthread_create",         lib_pthread},
  {relite_hook_pthread_join,            "pthread_join",           lib_pthread},
  {relite_hook_pthread_mutex_init,      "pthread_mutex_init",     lib_pthread},
  {relite_hook_pthread_mutex_destroy,   "pthread_mutex_destroy",  lib_pthread},
  {relite_hook_pthread_mutex_lock,      "pthread_mutex_lock",     lib_pthread},
  {relite_hook_pthread_mutex_trylock,   "pthread_mutex_trylock",  lib_pthread},
  {relite_hook_pthread_mutex_unlock,    "pthread_mutex_unlock",   lib_pthread},
  {relite_hook_pthread_cond_init,       "pthread_cond_init",      lib_pthread},
  {relite_hook_pthread_cond_destroy,    "pthread_cond_destroy",   lib_pthread},
  {relite_hook_pthread_cond_signal,     "pthread_cond_signal",    lib_pthread},
  {relite_hook_pthread_cond_broadcast,  "pthread_cond_broadcast", lib_pthread},
  {relite_hook_pthread_cond_wait,       "pthread_cond_wait",      lib_pthread},
  {relite_hook_pthread_cond_timedwait,  "pthread_cond_timedwait", lib_pthread},
};


//void                    relite_hook_init    () __attribute__((constructor(101)));

void                    relite_hook_init    () {
  DBG("hook init...");

  int i;
  for (i = 0; i != sizeof(libs)/sizeof(libs[0]); i += 1) {
    relite_lib_t* lib = &libs[i];
    if ((int)lib->id != i) {
      relite_fatal("lib '%s' has incorrect id: %d/%d",
                   lib->name, (int)lib->id, i);
    }
    lib->handle = dlopen(lib->name, RTLD_LAZY);
    if (lib->handle == 0) {
      relite_fatal("failed to load dynamic library '%s'", lib->name);
    }
  }

  for (i = 0; i != sizeof(hooks)/sizeof(hooks[0]); i += 1) {
    relite_hook_t* hook = &hooks[i];
    if ((int)hook->id != i) {
      relite_fatal("hook for function '%s' has incorrect id: %d/%d",
                   hook->name, (int)hook->id, i);
    }
    hook->func = dlsym(libs[hook->lib].handle, hook->name);
    if (hook->func == 0) {
      relite_fatal("failed to resolve symbol '%s' in '%s'",
            hook->name, libs[hook->lib].name);
    }
  }
  DBG("OK");
}


relite_hook_f           relite_hook_get      (relite_hook_e hook) {
  //write(1, "getting hook\n", sizeof("getting hook\n") - 1);
  //if (hooks[hook].func == 0)
  //  relite_hook_init();
  //assert(hooks[hook].func != 0);
  return hooks[hook].func;
}


void*                   relite_malloc       (size_t size) {
  /*
  typedef void* (*malloc_f) (size_t);
  malloc_f real_malloc = (malloc_f)relite_hook_get(relite_hook_malloc);
  if (real_malloc == 0)
    return 0;
  return real_malloc(size);
  */
  return mmap
      (0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}


void                    relite_free         (void* ptr) {
  if (ptr == 0)
    return;
  //TODO(dvyukov): total crap
  munmap(ptr, 4096);
  /*
  typedef void (*free_f) (void*);
  free_f real_free = (free_f)relite_hook_get(relite_hook_free);
  if (real_free == 0) {
    assert(ptr == 0);
    return;
  }
  real_free(ptr);
  */


}




