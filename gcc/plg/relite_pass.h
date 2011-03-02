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

#ifndef RELITE_PASS_H_INCLUDED
#define RELITE_PASS_H_INCLUDED

#include <config.h>
#include <system.h>
#include <tm.h>
#include <coretypes.h>
#include <function.h>
#include <gimple.h>


typedef struct relite_context_t relite_context_t;
typedef struct func_desc_t func_desc_t;
typedef struct expr_desc_t expr_desc_t;


struct func_desc_t {
  char const*           rt_name;
  char const*           real_name;
  tree                  fndecl;
};


struct expr_desc_t {
  location_t                loc;
  expanded_location const*  eloc;
  gimple_stmt_iterator*     gsi;
  tree                      expr;
};


typedef void            (*instrument_func)  (relite_context_t* ctx,
                                             expr_desc_t const* expr);


struct relite_context_t {
  int                   opt_debug;

  instrument_func       instr_enter;
  instrument_func       instr_leave;
  instrument_func       instr_store;
  instrument_func       instr_load;
  instrument_func       instr_func;

  func_desc_t*          rt_funcs;
  int                   rt_func_count;
  int                   rt_func_setup;

  int                   stat_func_total;
  int                   stat_func_instrumented;
  int                   stat_gimple;
  int                   stat_store_total;
  int                   stat_store_instrumented;
  int                   stat_load_total;
  int                   stat_load_instrumented;
};


void                    setup_context       (relite_context_t* ctx);


void                    relite_pass         (relite_context_t* ctx,
                                             struct function* func);


void                    relite_finish       (relite_context_t* ctx);


typedef enum rt_func_index {
  rt_enter,
  rt_leave,
  rt_store,
  rt_load,
  rt_acquire,
  rt_release,
  rt_call_desc_t,
  rt_mop_desc_t,
  rt_func_desc_t,
  rt_calls,
  rt_mops,
  rt_func_desc,
} rt_func_index;


tree                    rt_func             (rt_func_index idx);


#endif

