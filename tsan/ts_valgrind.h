/*
  This file is part of ThreadSanitizer, a dynamic data race detector 
  based on Valgrind.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com 

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Konstantin Serebryany.

#ifndef __TS_VALGRIND_H__
#define __TS_VALGRIND_H__

#include <stdint.h>
extern "C" {
#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_vki.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_errormgr.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_seqmatch.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_options.h"
} // extern "C"



enum {
  // Data race.
  XS_Race = 1234,
  // Unlocking a lock that is held by another thread.
  XS_UnlockForeign,
  // Unlocking a lock that is not locked.
  XS_UnlockNonLocked
};

extern uintptr_t GetVgPcOfCurrentThread();


// This stuff is temporary here.
// If/when valgrind is C++-ified, this will move to coregrind.

void PushMallocCostCenter(const char *cc);
void PopMallocCostCenter();

class ScopedMallocCostCenter {
 public:
  ScopedMallocCostCenter(const char *cc) {
#ifdef DEBUG
      PushMallocCostCenter(cc);
#endif
  }
  ~ScopedMallocCostCenter() {
#ifdef DEBUG
      PopMallocCostCenter();
#endif
  }
};



#endif //  __TS_VALGRIND_H__
// {{{1 end
// vim:shiftwidth=2:softtabstop=2:expandtab
