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

bool GetNameAndOffsetOfGlobalObject(uintptr_t addr,
                                    string *name, uintptr_t *offset) {
#ifdef TS_VALGRIND
    const int kBufLen = 1023;
    char buff[kBufLen+1];
    PtrdiffT off;
    if (VG_(get_datasym_and_offset)(addr, reinterpret_cast<Char*>(buff),
                                    kBufLen, &off)) {
      *name = buff;
      *offset = off;
      return true;
    }
    return false;
#else
  return false;
#endif // TS_VALGRIND
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

int OpenFileReadOnly(const string &file_name, bool die_if_failed) {
#ifdef TS_VALGRIND
  SysRes sres = VG_(open)((const Char*)file_name.c_str(), VKI_O_RDONLY, 0);
  if (sr_isError(sres)) {
    if (die_if_failed) {
      Report("ERROR: can not open file %s\n", file_name.c_str());
      exit(1);
    } else {
      return -1;
    }
  }
  return sr_Res(sres);
#else // no TS_VALGRIND
  UNIMPLEMENTED();
#endif
}

// Read the contents of a file to string. Valgrind version.
string ReadFileToString(const string &file_name, bool die_if_failed) {
  int fd = OpenFileReadOnly(file_name, die_if_failed);
  if (fd == -1) {
    return string();
  }
  char buff[257] = {0};
  int n_read;
  string res;
  while ((n_read = read(fd, buff, sizeof(buff) - 1)) > 0) {
    buff[n_read] = 0;
    res += buff;
  }
  close(fd);
  return res;
}

size_t GetVmSizeInMb() {
#ifdef VGO_linux
  static int fd = -2;
  if (fd == -2) {  // safe since valgrind is single-threaded.
    fd = OpenFileReadOnly("/proc/self/status", false);
  }
  if (fd < 0) return 0;
  char buff[10 * 1024];
  VG_(lseek)(fd, 0, SEEK_SET);
  int n_read = read(fd, buff, sizeof(buff) - 1);
  buff[n_read] = 0;
  const char *vm_size_name = "VmSize:";
  const int   vm_size_name_len = 7;
  const char *vm_size_str = (const char *)VG_(strstr)((Char*)buff,
                                                      (Char*)vm_size_name);
  if (!vm_size_str) return 0;
  vm_size_str += vm_size_name_len;
  while(*vm_size_str == ' ') vm_size_str++;
  char *end;
  size_t vm_size_in_kb = my_strtol(vm_size_str, &end);
  return vm_size_in_kb >> 10;
#else
  return 0;
#endif
}

void OpenFileWriteStringAndClose(const string &file_name, const string &str) {
#ifdef TS_VALGRIND
  SysRes sres = VG_(open)((const Char*)file_name.c_str(),
                          VKI_O_WRONLY|VKI_O_CREAT|VKI_O_TRUNC,
                          VKI_S_IRUSR|VKI_S_IWUSR);
  if (sr_isError(sres)) {
    Report("WARNING: can not open file %s\n", file_name.c_str());
    exit(1);
  }
  int fd = sr_Res(sres);
  write(fd, str.c_str(), str.size());
  close(fd);
#else
  UNIMPLEMENTED();
#endif
}

// This function is taken from valgrind's m_libcbase.c (thanks GPL!).
static bool FastRecursiveStringMatch(const char* pat, const char* str,
                                     int *depth) {
  CHECK_LT((*depth), 10000);
  (*depth)++;
  for (;;) {
    switch (*pat) {
      case '\0':(*depth)--;
                return (*str == '\0');
      case '*': do {
                  if (FastRecursiveStringMatch(pat+1, str, depth)) {
                    (*depth)--;
                    return true;
                  }
                } while (*str++);
                  (*depth)--;
                  return false;
      case '?': if (*str++ == '\0') {
                  (*depth)--;
                  return false;
                }
                pat++;
                break;
      case '\\':if (*++pat == '\0') {
                  (*depth)--;
                  return false; /* spurious trailing \ in pattern */
                }
                /* falls through to ... */
      default : if (*pat++ != *str++) {
                  (*depth)--;
                  return false;
                }
                break;
    }
  }
}

bool StringMatch(const string &pattern, const string &str) {
  int depth = 0;
  return FastRecursiveStringMatch(pattern.c_str(), str.c_str(), &depth);
}

// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
