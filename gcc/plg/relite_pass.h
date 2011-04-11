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


typedef struct relite_context_t {
  int                   opt_debug;
  int                   opt_stat;
  int                   opt_sblock_size;
  char const*           opt_ignore;

  int                   setup_completed;
  tree                  rtl_stack;  // thread local shadow stack
  tree                  rtl_ignore; // thread local recursive ignore
  tree                  rtl_mop;    // mop handling function
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
} relite_context_t;


void                    relite_prepass      (relite_context_t* ctx);
void                    relite_pass         (relite_context_t* ctx);
void                    relite_finish       (relite_context_t* ctx);


#endif

