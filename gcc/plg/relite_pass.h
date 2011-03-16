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

#ifndef RELITE_PASS_H_INCLUDED
#define RELITE_PASS_H_INCLUDED

#include "relite_ignore.h"


typedef struct rt_decl_desc_t {
  char const*           rt_name;
  char const*           real_name;
  tree                  decl;
} rt_decl_desc_t;


typedef struct relite_context_t {
  void                  (*setup)            (struct relite_context_t* ctx);

  void                  (*instr_func)       (struct relite_context_t* ctx,
                                             tree func_decl,
                                             gimple_seq* pre,
                                             gimple_seq* post);

  void                  (*instr_mop)        (struct relite_context_t* ctx,
                                             tree expr,
                                             location_t loc,
                                             int is_store,
                                             int is_sblock,
                                             gimple_seq* pre,
                                             gimple_seq* post);

  void                  (*instr_call)       (struct relite_context_t* ctx,
                                             tree func_decl,
                                             location_t loc,
                                             gimple_seq* pre,
                                             gimple_seq* post);

  int                   opt_debug;
  int                   opt_sblock_size;
  char const*           opt_ignore;

  int                   setup_completed;
  int                   ignore_file;
  int                   func_calls;
  int                   func_mops;
  relite_ignore_e       func_ignore;

  int                   stat_func_total;
  int                   stat_func_instrumented;
  int                   stat_gimple;
  int                   stat_store_total;
  int                   stat_store_instrumented;
  int                   stat_load_total;
  int                   stat_load_instrumented;
  int                   stat_sblock;
  int                   stat_bb_total;
  //int                   stat_bb_super;
} relite_context_t;


relite_context_t*       create_context      ();


void                    relite_prepass      (relite_context_t* ctx);

void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func);


void                    relite_finish       (relite_context_t* ctx);


#endif

