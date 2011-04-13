/* Compiler instrumentation test suite (CITS)
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * CITS is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#pragma once
#include <string>


struct mop_desc_t {
  void*                 addr;
  void*                 pc;
  int                   is_store;
  int                   is_sblock;
  int                   msize;
  int                   source_line;
};


void                    frontend_init       ();
void                    frontend_test_begin ();
bool                    frontend_test_end   (std::string* error_desc);
void                    frontend_mop_cb     (mop_desc_t const& mop);


