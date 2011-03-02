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
#include "relite_stdlib.h"
#include "relite_hook.h"
#include "relite_defs.h"
#include "relite_dbg.h"
#include <sys/mman.h>
#include <unistd.h>
#include <memory.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>


void* __libc_realloc(void *ptr, size_t size);


typedef struct mem_hdr_t {
  size_t                size;
  size_t                pad;
  //TODO(dvyukov) add info about allocation site
} mem_hdr_t;


#define                 MINE_ZONE           32
#define                 MEM_ALIGNMENT       16
#define                 ADD_SIZE            (sizeof(mem_hdr_t) + 2 * MINE_ZONE)


static void*            relite_malloc_impl         (size_t size, void* (*alloc_func)(size_t)) {
  assert((ADD_SIZE % MEM_ALIGNMENT) == 0);
  //DBG("malloc(%zu)", size);
  char* const mem = (char*)alloc_func(size + ADD_SIZE);
  mem_hdr_t* const hdr = (mem_hdr_t*)mem;
  hdr->size = size;
  char* const p = mem +  MINE_ZONE + sizeof(mem_hdr_t);
  handle_mem_init(mem, p, STATE_MINE_ZONE);
  //handle_mem_init(p, p + size, STATE_UNITIALIZED);
  handle_mem_alloc(p, p + size);
  handle_mem_init(p + size, p + size + MINE_ZONE, STATE_MINE_ZONE);
  //DBG("malloc(%zu)=%p", size, p);
  return p;
}


static char g_buf [16*1024*1024];
static char* g_pos = g_buf;


void*                   malloc              (size_t size) {
  typedef void* (*real_f) (size_t);
  real_f real = (real_f)relite_hook_get(relite_hook_malloc);
  if (real == 0) {
    //write(1, "MALLOC STUB\n", sizeof("MALLOC STUB\n") - 1);
    void* mem = g_pos;
    g_pos += size;
    if (g_pos > g_buf + sizeof(g_buf)) {
      DBG("static alloc is exhausted");
      exit(1);
    }
    return mem;
  } else {
    //write(1, "MALLOC REAL\n", sizeof("MALLOC REAL\n") - 1);
    return relite_malloc_impl(size, real);
  }
}

/*
void* __libc_malloc(size_t size) {
  write(1, "__libc_malloc\n", sizeof("__libc_malloc\n") - 1);
  return malloc(size);
}
*/

/*
void*                   relite_new          (size_t size) {
  return relite_malloc_impl(size, &malloc);
}
*/


void*                   calloc       (size_t nmemb, size_t size) {
  void* mem = malloc(nmemb * size);
  memset(mem, 0, nmemb * size);
  return mem;

  /*
  void* p = calloc(nmemb, size);
  DBG("calloc(%zu, %zu)=%p", nmemb, size, p);
  return p;
  */
}


void*                   realloc      (void* p, size_t size) {
  if (size == 0) {
    free(p);
    return 0;
  } else {
    void* mem = malloc(size);
    if (p != 0) {
      mem_hdr_t* hdr = (mem_hdr_t*)((char*)p - MINE_ZONE - sizeof(mem_hdr_t));
      memcpy(mem, p, hdr->size);
      free(p);
    }
    return mem;
  }

  /*
  typedef void* (*real_f) (void*, size_t);
  real_f real = (real_f)relite_hook_get2(relite_hook_realloc);
  return real(p, size);

  */

  /*
  void* p2 = realloc(p, size);

  DBG("realloc(%p, %zu)=%p", p, size, p2);
  return p2;
  */
}


void                    free               (void* p) {
  if (p == 0)
    return;
  typedef void (*real_f)(void*);
  real_f real = (real_f)relite_hook_get(relite_hook_free);
  if (real == 0 || ((char*)p >= g_buf && (char*)p < g_pos)) {
    //write(1, "FREE STUB\n", sizeof("FREE STUB\n") - 1);
  } else {
    //write(1, "FREE REAL\n", sizeof("FREE REAL\n") - 1);
    char* const mem = (char*)p - MINE_ZONE - sizeof(mem_hdr_t);
    mem_hdr_t* hdr = (mem_hdr_t*)mem;
    size_t const size = hdr->size;
    handle_mem_free(mem, mem + size + ADD_SIZE);
    //TODO(dvyukov): delay actual free somewhat
    //DBG("free(%p)", p);
    real(mem);
  }
}

/*
void __libc_free(void* p) {
  write(1, "__libc_free\n", sizeof("__libc_free\n") - 1);
  free(p);
}
*/

/*
void                    relite_delete       (void* p) {
  relite_free(p);
}
*/


void*                   memset              (void* s,
                                             int c,
                                             size_t n) {
  DBG("memset(%p, %d, %zd)", s, c, n);
  if (n == 0)
    return s;
  handle_region_store(s, (char*)s + n);
  return relite_memset(s, c, n);
}


void*                   memcpy              (void* dst,
                                             void const* src,
                                             size_t n) {
  DBG("memcpy(%p, %p, %zd)", dst, src, n);
  if (n == 0)
    return dst;
  handle_region_load(src, (char*)src + n);
  handle_region_store(dst, (char*)dst + n);

  typedef void* (*real_f)(void* dst, void const* src, size_t n);
  real_f real_memcpy = (real_f)relite_hook_get(relite_hook_memcpy);
  if (real_memcpy != 0) {
    return real_memcpy(dst, src, n);
  } else {
    char* dst_pos = (char*)dst;
    char* dst_end = dst_pos + n;
    char const* src_pos = (char const*)src;
    for (; dst_pos != dst_end; dst_pos += 1, src_pos += 1)
      *dst_pos = *src_pos;
    return dst;
  }
}


int                     memcmp              (void const* s1,
                                             void const* s2,
                                             size_t n) {
  DBG("memcmp(%p, %p, %zd)", s1, s2, n);
  if (n == 0)
    return 0;
  handle_region_load(s1, (char*)s1 + n);
  handle_region_load(s2, (char*)s2 + n);
  return relite_memcmp(s1, s2, n);
}


int                     relite_memcmp       (void const* s1,
                                             void const* s2,
                                             size_t n) {
  typedef int (*real_f)(void const* s1, void const* s2, size_t n);
  real_f real_memcmp = (real_f)relite_hook_get(relite_hook_memcmp);
  if (real_memcmp != 0) {
    return real_memcmp(s1, s2, n);
  } else {
    unsigned char const* s1_pos = (unsigned char const*)s1;
    unsigned char const* s2_pos = (unsigned char const*)s2;
    unsigned char const* s1_end = s1_pos + n;
    for (; s1_pos != s1_end; s1_pos += 1, s1_pos += 1) {
      int cmp = *s1_pos - *s2_pos;
      if (cmp == 0)
        continue;
      else
        return cmp;
    }
    return 0;
  }
}


void*                   relite_memset       (void* s,
                                             int c,
                                             size_t n) {
  typedef void* (*real_f)(void* s, int c, size_t n);
  real_f real_memset = (real_f)relite_hook_get(relite_hook_memset);
  if (real_memset != 0) {
    return real_memset(s, c, n);
  } else {
    char* pos = (char*)s;
    char* end = pos + n;
    for (; pos != end; pos += 1)
      *pos = (char)c;
    return s;
  }
}



void* __real_mmap(void *addr, size_t length,
                  int prot, int flags, int fd, off_t offset);
/*
int __real_munmap(void *addr, size_t length);
*/

void* __wrap_mmap2(void *addr, size_t length,
                  int prot, int flags, int fd, off_t offset) {
  //TODO(dvyukov): handle read-only mappings
  //printf("relite: mmap(%p, %zu)\n", addr, length);
  void* mem = mmap(addr, length, prot, flags, fd, offset);
  if (mem != (void*)-1)
    handle_mem_alloc(mem, mem + length);
  //printf("relite: mmap(%p, %zu)=%p\n", addr, length, mem);
  return mem;
}



void* __wrap_mmap64(void *addr, size_t length,
                  int prot, int flags, int fd, off_t offset) {
  //TODO(dvyukov): handle read-only mappings
  //printf("relite: mmap(%p, %zu)\n", addr, length);
  void* mem = mmap(addr, length, prot, flags, fd, offset);
  if (mem != (void*)-1)
    handle_mem_alloc(mem, mem + length);
  //printf("relite: mmap(%p, %zu)=%p\n", addr, length, mem);
  return mem;
}


void* __wrap_mmap(void *addr, size_t length,
                  int prot, int flags, int fd, off_t offset) {
  //TODO(dvyukov): handle read-only mappings
  //printf("relite: mmap(%p, %zu)\n", addr, length);
  void* mem = mmap(addr, length, prot, flags, fd, offset);
  if (mem != (void*)-1)
    handle_mem_alloc(mem, mem + length);
  //printf("relite: mmap(%p, %zu)=%p\n", addr, length, mem);
  return mem;
}


int __wrap_munmap(void *addr, size_t length) {
  //printf("relite: munmap(%p, %zu)\n", addr, length);
  handle_mem_free(addr, addr + length);
  return munmap(addr, length);
}


