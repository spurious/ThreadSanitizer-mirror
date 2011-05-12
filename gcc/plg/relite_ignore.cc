/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

#include "relite_ignore.h"
#include "../../tsan/ignore.h"
#include "../../tsan/common_util.h"
#include "../../tsan/thread_sanitizer.h"
//#include <stdarg.h>


static IgnoreLists* g_ignore;


void                    relite_ignore_init  (char const* file_name) {
  if (file_name == 0 || file_name[0] == 0)
    return;
  G_flags = new (calloc(sizeof(FLAGS), 1)) FLAGS;
  std::string const& file_data = ReadFileToString(file_name, true);
  if (file_data.empty())
    return;
  g_ignore = new IgnoreLists;
  ReadIgnoresFromString(file_data, g_ignore);
}


int                     relite_ignore_file  (char const* file) {
  if (g_ignore != 0) {
    return TripleVectorMatchKnown(g_ignore->ignores,
                                  std::string(),
                                  std::string(),
                                  file);
  } else {
    return 0;
  }
}


relite_ignore_e         relite_ignore_func  (char const* func) {
  if (g_ignore != 0) {
    std::string const empty;
    std::string const func_str (func);
    if (TripleVectorMatchKnown(g_ignore->ignores_r,
                               func_str, empty, empty)) {
      return relite_ignore_rec;
    } else if (TripleVectorMatchKnown(g_ignore->ignores_hist,
                                      func_str, empty, empty)) {
      return relite_ignore_hist;
    } else if (TripleVectorMatchKnown(g_ignore->ignores,
                                      func_str, empty, empty)) {
      return relite_ignore_mop;
    }
  }
  return relite_ignore_none;
}


/*
void                    Report              (const char *format, ...) {
  // Required by OpenFileReadOnly in common_util.h
  std::vector<char> buff (4*1024);

  va_list args;
  for (;;) {
    va_start(args, format);
    int ret = vsnprintf(&buff[0], buff.size(), format, args);
    va_end(args);
    if ((size_t)ret < buff.size())
      break;
    buff.resize(buff.size() * 2);
  }
  printf("%s\n", &buff[0]);
}


void                    Printf              (const char *format, ...)
   __attribute__((alias("Report")));
*/


