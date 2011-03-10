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

#ifndef RELITE_RT_H_INCLUDED
#define RELITE_RT_H_INCLUDED
#ifdef __cplusplus
extern "C" {
#endif

//#include "relite_pthread.h"
//#include "relite_stdlib.h"

/*
typedef struct relite_call_stack_t {
  void**    top;
  void*     base [kMaxCallStackSize];
} relite_call_stack_t;
*/

extern __thread void** ShadowStack;
extern __thread void* INFO;

void tsan_rtl_mop(void* addr, unsigned flags);

void ThreadSanitizerHandleOneMemoryAccess
          (void* thr, unsigned long long desc, void* addr);

void    relite_enter                  (void const volatile*);
void    relite_leave                  ();

void    relite_store                  (void const volatile* addr,
                                       unsigned flags);

void    relite_load                   (void const volatile* addr,
                                       unsigned flags);

void    relite_acquire                (void const volatile* addr);
void    relite_release                (void const volatile* addr);


typedef enum relite_report_type_e {
  relite_report_race_load,
  relite_report_race_store,
  relite_report_use_after_free,
  relite_report_unitialized,
  relite_report_out_of_bounds,
} relite_report_type_e;


typedef struct relite_report_t {
  void const volatile*                      addr;
  int                                       size;
  relite_report_type_e                      type;
} relite_report_t;

char const*             relite_report_str   (relite_report_type_e type);

void                    relite_report_hook  (int(*)(relite_report_t const*));


#ifdef __cplusplus
}
#endif
#endif



