/* Copyright (c) 2010-2011, Google Inc.
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

// Author: <Dmitry Vyukov> dvyukov@google.com

#include "ts_util.h"
#include "tsan_rtl_wrap.h"
#include <dlfcn.h>

namespace __tsan {

memchr_ft               real_memchr;
pthread_create_ft       real_pthread_create;
posix_memalign_ft       real_posix_memalign;
valloc_ft               real_valloc;
memalign_ft             real_memalign;

void WrapInit() {
  real_memchr = (memchr_ft)dlsym(RTLD_NEXT, "memchr");
  real_pthread_create = (pthread_create_ft)dlsym(RTLD_NEXT, "pthread_create");
  real_posix_memalign = (posix_memalign_ft)dlsym(RTLD_NEXT, "posix_memalign");
  real_valloc = (valloc_ft)dlsym(RTLD_NEXT, "valloc");
  real_memalign = (memalign_ft)dlsym(RTLD_NEXT, "memalign");
}

} // namespace __tsan
