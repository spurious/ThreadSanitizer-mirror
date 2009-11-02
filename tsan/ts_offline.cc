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

// Experimental off-line race detector.

#include "thread_sanitizer.h"

#include <stdio.h>
#include <stdarg.h>


int main() {
  printf("INFO: ThreadSanitizerOffline\n");

}


//---------------------- ThreadSanitizerOffline -- {{{1
void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  *img_name = "unimplemented";
  *rtn_name = "unimplemented";
  *file_name = "unimplemented";
  *line_no = 0;
}

string PcToRtnName(uintptr_t pc, bool demangle) {
  return string("unimplemented");
}

//---------------------- Utils ------------------- {{{1
// TODO(kcc): move it somewhere else.

void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

extern "C"
long my_strtol(const char *str, char **end) {
  if (str && str[0] == '0' && str[1] == 'x') {
    return strtoll(str, end, 16);
  }
  return strtoll(str, end, 10);
}
