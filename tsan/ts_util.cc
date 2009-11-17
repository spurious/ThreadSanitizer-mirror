/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

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
// Author: Timur Iskhodzhanov.
//
// See ts_util.h for mode details.

#include "thread_sanitizer.h"
#include <stdarg.h>


bool RecordErrorIfNotSuppressed(ThreadSanitizerReport *report) {
#ifdef TS_VALGRIND
  // Record an error using standard valgrind machinery.
  // TODO(kcc): migrate to our own system (when ready).
  CHECK(ThreadSanitizerReport::DATA_RACE == 0);
  return ERROR_IS_RECORDED == VG_(maybe_record_error)(
      GetVgTid(), report->type + XS_Race, 0, NULL, report);
#else 
  // TODO(kcc): implement suppressions.
  ThreadSanitizerPrintReport(report);
  return true;
#endif
}

#ifndef TS_VALGRIND
FILE *G_out = stdout;
#endif

void Printf(const char *format, ...) {
#ifdef TS_VALGRIND
  va_list args;
  va_start(args, format);
  VG_(vprintf)(format, args);
  va_end(args);
#else
  va_list args;
  va_start(args, format);
  vfprintf(G_out, format, args);
  va_end(args);
#endif
}

long my_strtol(const char *str, char **end) {
#ifdef TS_VALGRIND
  if (str && str[0] == '0' && str[1] == 'x') {
    return VG_(strtoll16)((Char*)str, (Char**)end);
  }
  return VG_(strtoll10)((Char*)str, (Char**)end);
#else
  if (str && str[0] == '0' && str[1] == 'x') {
    return strtoll(str, end, 16);
  }
  return strtoll(str, end, 10);
#endif
}
