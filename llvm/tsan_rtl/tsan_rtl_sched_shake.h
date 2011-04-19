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

#ifndef TSAN_RTL_SCHED_SHAKE_H_INCLUDED
#define TSAN_RTL_SCHED_SHAKE_H_INCLUDED

#include "thread_sanitizer.h"

//#define TSAN_SCHED_SHAKE
//#define TSAN_API_AMBUSH

#if defined(TSAN_SCHED_SHAKE) || defined(TSAN_API_AMBUSH)
unsigned tsan_rtl_rand();
#endif

#ifdef TSAN_SCHED_SHAKE
#define SCHED_SHAKE(heavy) \
  do { if (G_flags->sched_shake) tsan_rtl_sched_shake(!!(heavy)); } while ((void)0, 0)
void tsan_rtl_sched_shake(bool heavy);
#else
#define SCHED_SHAKE(...) (void)0
#endif

#ifdef TSAN_API_AMBUSH
#define API_AMBUSH() (G_flags->api_ambush)
#else
#define TSAN_API_AMBUSH() ((void)0,0)
#endif

#endif


