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

#ifndef RELITE_STDLIB_H_INCLUDED
#define RELITE_STDLIB_H_INCLUDED

#include <stddef.h>

/*
//void*                   relite_malloc       (size_t size);
void*                   relite_new          (size_t size);
void*                   relite_calloc       (size_t nmemb, size_t size);
void*                   relite_realloc      (void* p, size_t size);
//void                    relite_free         (void* p);
void                    relite_delete       (void* p);


void*                   relite_memset       (void* s,
                                             int c,
                                             size_t n);

void*                   relite_memcpy       (void* dst,
                                             void const* src,
                                             size_t n);

int                     relite_memcmp       (void const* s1,
                                             void const* s2,
                                             size_t n);
*/

void*                   relite_memset       (void* s,
                                             int c,
                                             size_t n);

int                     relite_memcmp       (void const* s1,
                                             void const* s2,
                                             size_t n);


#endif

