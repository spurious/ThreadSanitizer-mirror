/* Compiler instrumentation test suite (CITS)
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * CITS is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#include "frontend.h"
#include "../gcc/plg/relite_rt.h"
#include "../llvm/tsan_rtl/tsan_rtl_lbfd.h"


__thread void**         ShadowStack;
__thread int            thread_local_ignore;
void**                  shadow_stack_base;


void                    frontend_init       () {
  if (false == tsan_rtl_lbfd::BfdInit())
    printf("failed to initialize bfd\n");
  shadow_stack_base = new void* [1024]; 
}


void                    frontend_test_begin () {
  ShadowStack = shadow_stack_base;    
}


bool                    frontend_test_end   (std::string* error_desc) {
  if (ShadowStack != shadow_stack_base) {
    *error_desc = "CORRUPTED SHADOW STACK";
    return false;
  }
  return true;
}



void                    tsan_rtl_mop        (void* addr, unsigned flags) {
  mop_desc_t mop = {};
  mop.addr = addr;
  mop.pc = __builtin_return_address(0);
  mop.is_sblock = !!(flags & 1);
  mop.is_store = !!(flags & 2);
  mop.msize = (flags >> 2) + 1;
  tsan_rtl_lbfd::BfdPcToStrings((pc_t)mop.pc, false, 0, 0, 0, &mop.source_line);
  frontend_mop_cb(mop);
}

