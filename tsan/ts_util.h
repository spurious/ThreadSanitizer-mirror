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

// This file contains utility classes and functions used by ThreadSanitizer.
// TODO(kcc): move more utilities from thread_sanitizer.cc to this file.

#ifndef __TS_UTIL_H__
#define __TS_UTIL_H__


class ThreadSanitizerReport;

// Returns true if the error has been recorded.
bool RecordErrorIfNotSuppressed(ThreadSanitizerReport *report);

extern "C" long my_strtol(const char *str, char **end);
extern void Printf(const char *format, ...);


int OpenFileReadOnly(const string &file_name, bool die_if_failed);
string ReadFileToString(const string &file_name, bool die_if_failed);

// Get the current memory footprint of myself (parse /proc/self/status).
size_t GetVmSizeInMb();

// Sets the contents of the file 'file_name' to 'str'.
void OpenFileWriteStringAndClose(const string &file_name, const string &str);


// Match a wild card which may contain '*' and '?'.
bool StringMatch(const string &pattern, const string &str);

// If addr is inside a global object, returns true and sets 'name' and 'offset'
bool GetNameAndOffsetOfGlobalObject(uintptr_t addr,
                                    string *name, uintptr_t *offset);


inline uintptr_t tsan_bswap(uintptr_t x) {
#if __WORDSIZE == 64
  // return __builtin_bswap64(x);
  __asm__("bswapq %0" : "=r" (x) : "0" (x));
  return x;
#elif __WORDSIZE == 32
  // return __builtin_bswap32(x);
  __asm__("bswapl %0" : "=r" (x) : "0" (x));
  return x;
#else
# error  "Unknown __WORDSIZE"
#endif // __WORDSIZE
}




#endif  // __TS_UTIL_H__
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
