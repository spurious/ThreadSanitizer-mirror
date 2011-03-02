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


#ifndef RELITE_RT_INT_H_INCLUDED
#define RELITE_RT_INT_H_INCLUDED

#include "relite_rt.h"
#include "relite_defs.h"

//TODO(dvyukov): start all functions with relite prefix

unsigned                relite_rand         (unsigned limit);
void                    relite_sched_shake  ();

void                    handle_thread_start ();
void                    handle_thread_end   ();

void                    handle_sync_create  (void const volatile* addr);

void                    handle_sync_destroy (void const volatile* addr);

void                    handle_sync_acquire (void const volatile* addr,
                                             int is_mtx);

void                    handle_sync_release (void const volatile* addr,
                                             int is_mtx);

void                    handle_region_load  (void const volatile* begin,
                                             void const volatile* end);

void                    handle_region_store (void const volatile* begin,
                                             void const volatile* end);

void                    handle_mem_init     (addr_t begin,
                                             addr_t end,
                                             state_t state);

void                    handle_mem_alloc    (addr_t begin,
                                             addr_t end);

void                    handle_mem_free     (addr_t begin,
                                             addr_t end);

#endif

