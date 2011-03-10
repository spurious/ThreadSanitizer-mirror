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

#ifndef RELITE_DEFS_H_INCLUDED
#define RELITE_DEFS_H_INCLUDED

#include <stdint.h>

//#define RELITE_API_AMBUSH
#define RELITE_SCHED_SHAKE


typedef                 void const volatile*addr_t;
typedef                 uint32_t            thrid_t;
typedef                 uint64_t            timestamp_t;
typedef                 uint64_t            state_t;


//#define MAX_THREADS             (64*1024)
#define MAX_THREADS             (1000)
#define THR_MASK_SIZE           (MAX_THREADS / sizeof(size_t) / 8)
#define SHADOW_BASE             ((atomic_uint64_t*)0x00000D0000000000ull)
#define SHADOW_SIZE             (0x0000800000000000ull - 0x00000E38E38E3800ull)
#define STATE_SYNC_MASK         0x8000000000000000ull
#define STATE_SYNC_SHIFT        63
#define STATE_SIZE_MASK         0x6000000000000000ull
#define STATE_SIZE_SHIFT        61
#define STATE_LOAD_MASK         0x1000000000000000ull
#define STATE_LOAD_SHIFT        60
#define STATE_THRID_MASK        0x0FFFF00000000000ull
#define STATE_THRID_SHIFT       44
#define STATE_TIMESTAMP_MASK    0x00000FFFFFFFFFFFull

#define STATE_UNITIALIZED       0x00000FFFFFFFFFFFull
#define STATE_MINE_ZONE         0x00000FFFFFFFFFFEull
#define STATE_FREED             0x00000FFFFFFFFFFDull

#define SZ_1                    3
#define SZ_2                    2
#define SZ_4                    1
#define SZ_8                    0


#endif

