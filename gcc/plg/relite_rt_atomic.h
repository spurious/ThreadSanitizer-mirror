/* Relite: ThreadSanitizer runtime stub
 * Copyright (c) 2011, Google Inc. All rights reserved.
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

#pragma once
#include "../../tsan/ts_atomic.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void      tsan_atomic_fence                     (tsan_memory_order mo);
uint64_t  tsan_atomic_load                      (void const volatile* a,
                                                 int size,
                                                 tsan_memory_order mo);
void      tsan_atomic_store                     (void volatile* a,
                                                 int size,
                                                 uint64_t v,
                                                 tsan_memory_order mo);
uint64_t  tsan_atomic_fetch_op                  (void volatile* a,
                                                 int size,
                                                 uint64_t v,
                                                 tsan_memory_op op,
                                                 tsan_memory_order mo);
uint64_t  tsan_atomic_compare_exchange          (void volatile* a,
                                                 int size,
                                                 int is_strong,
                                                 uint64_t cmp,
                                                 uint64_t xch,
                                                 tsan_memory_order mo,
                                                 tsan_memory_order fail_mo);

#ifdef __cplusplus
}
#endif

