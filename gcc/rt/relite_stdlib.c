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
#include "relite_dbg.h"
#include <memory.h>



void*                   relite_memset        (void* s,
                                             int c,
                                             size_t n) {
  DBG("memset(%p, %d, %zd)", s, c, n);
  handle_region_store(s, (char*)s + n);
  return memset(s, c, n);
}


void*                   relite_memcpy        (void* dst,
                                             void const* src,
                                             size_t n) {
  DBG("memcpy(%p, %p, %zd)", dst, src, n);
  handle_region_load(src, (char*)src + n);
  handle_region_store(dst, (char*)dst + n);
  return memcpy(dst, src, n);
}


int                     relite_memcmp        (void const* s1,
                                              void const* s2,
                                              size_t n) {
  DBG("memcmp(%p, %p, %zd)", s1, s2, n);
  handle_region_load(s1, (char*)s1 + n);
  handle_region_load(s2, (char*)s2 + n);
  return memcmp(s1, s2, n);
}













