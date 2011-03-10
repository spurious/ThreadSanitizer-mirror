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

#include "relite_dbg.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>



//static pthread_mutex_t g_dbg_mtx = PTHREAD_MUTEX_INITIALIZER;
extern __thread thrid_t relite_dbg_tid;


void relite_dbg(char const* fmt, ...) {
  char buf1 [1024];
  snprintf(buf1, sizeof(buf1) - 1, "relite(%d): %s\n", relite_dbg_tid, fmt);

  va_list argptr;
  va_start(argptr, fmt);
  char buf2 [1024];
  vsnprintf(buf2, sizeof(buf2) - 1, buf1, argptr);
  va_end(argptr);

  write(1, buf2, strlen(buf2));
}


void                    relite_fatal        (char const* fmt, ...) {
  char buf1 [1024];
  snprintf(buf1, sizeof(buf1) - 1, "relite(%d): %s\n", relite_dbg_tid, fmt);

  va_list argptr;
  va_start(argptr, fmt);
  char buf2 [1024];
  vsnprintf(buf2, sizeof(buf2) - 1, buf1, argptr);
  va_end(argptr);

  write(1, buf2, strlen(buf2));
  exit(1);
}



