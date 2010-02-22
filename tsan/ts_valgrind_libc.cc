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
// Implement some of the libc functions to link with valgrind.
// Do not include any linux header here to avoid conflicts.
//
extern "C" {
#include "pub_tool_basics.h"
#include "pub_tool_libcbase.h"
}

// can't use VG_(memmove) since it is buggy.
extern "C" void * memmove(void *a, const void *b, unsigned long size) {
  char *A = (char*)a;
  const char *B = (const char*)b;
  if (A < B) {
    for (unsigned long i = 0; i < size; i++) {
      A[i] = B[i];
    }
  } else if(A > B) {
    for (unsigned long i = 0; i < size; i++) {
      A[size - i - 1] = B[size - i - 1];
    }
  }
  return a;
}

extern "C" int memcmp(const void *a, const void *b, unsigned long c) {
  return VG_(memcmp)(a,b,c);
}
