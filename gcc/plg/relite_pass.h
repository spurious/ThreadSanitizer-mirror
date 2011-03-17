/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
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

