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
#include "ts_stats.h"
#include <stdarg.h>

Stats *G_stats;

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
void GetThreadStack(int tid, uintptr_t *min_addr, uintptr_t *max_addr) {
  // UNIMPLEMENTED
  *min_addr = 0xfffa;
  *max_addr = 0xfffb;
}
#endif

static int n_errs_found;

void SetNumberOfFoundErrors(int n_errs) {
  n_errs_found = n_errs;
}

int GetNumberOfFoundErrors() {
  return n_errs_found;
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

// Like Print(), but prepend each line with ==XXXXX==,
// where XXXXX is the pid.
void Report(const char *format, ...) {
  int buff_size = 1024*16;
  char *buff = new char[buff_size];
  CHECK(buff);

  va_list args;

  while (1) {
    va_start(args, format);
    int ret = vsnprintf(buff, buff_size, format, args);
    va_end(args);
    if (ret < buff_size) break;
    delete [] buff;
    buff_size *= 2;
    buff = new char[buff_size];
    CHECK(buff);
    // Printf("Resized buff: %d\n", buff_size);
  }

  char pid_buff[100];
  snprintf(pid_buff, sizeof(pid_buff), "==%d== ", getpid());

  string res;
  int len = strlen(buff);
  bool last_was_new_line = true;
  for (int i = 0; i < len; i++) {
    if (last_was_new_line)
      res += pid_buff;
    last_was_new_line = (buff[i] == '\n');
    res += buff[i];
  }

  delete [] buff;

  Printf("%s", res.c_str());
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


#if defined(__GNUC__)
  typedef int TS_FILE;
  #define TS_FILE_INVALID (-1)
#elif defined(_MSC_VER) 
  typedef FILE *TS_FILE;
  #define TS_FILE_INVALID (NULL)
  #define read(fd, buf, size) fread(buf, size, 1, fd)
  #define close fclose
#endif



TS_FILE OpenFileReadOnly(const string &file_name, bool die_if_failed) {
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
#elif defined(_MSC_VER)
  return fopen(file_name.c_str(), "r");
#else // no TS_VALGRIND
  return open(file_name.c_str(), O_RDONLY);
#endif
}

// Read the contents of a file to string. Valgrind version.
string ReadFileToString(const string &file_name, bool die_if_failed) {
  TS_FILE fd = OpenFileReadOnly(file_name, die_if_failed);
  if (fd == TS_FILE_INVALID) {
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

bool StringMatch(const string& wildcard, const string& text) {
  const char* c_text = text.c_str();
  const char* c_wildcard = wildcard.c_str();
  // Start of the current look-ahead. Everything before these positions is a
  // definite, optimal match.
  const char* c_text_last = NULL;
  const char* c_wildcard_last = NULL;
  while (*c_text) {
    if (*c_wildcard == '*') {
      while (*++c_wildcard == '*') {
        // Skip all '*'.
      }
      if (!*c_wildcard) {
        // Ends with a series of '*'.
        return true;
      }
      c_text_last = c_text;
      c_wildcard_last = c_wildcard;
    } else if ((*c_text == *c_wildcard) || (*c_wildcard == '?')) {
      ++c_text;
      ++c_wildcard;
    } else if (c_text_last) {
      // No match. But we have seen at least one '*', so rollback and try at the
      // next position.
      c_wildcard = c_wildcard_last;
      c_text = c_text_last++;
    } else {
      return false;
    }
  }

  // Skip all '*' at the end of the wildcard.
  while (*c_wildcard == '*') {
    ++c_wildcard;
  }

  return !*c_wildcard;
}

// ---------------- Lite Race ------------------ {{{2
// Experimental!
//
// The idea was first introduced in LiteRace:
// http://www.cs.ucla.edu/~dlmarino/pubs/pldi09.pdf
// Instead of analyzing all memory accesses, we do sampling.
// For each trace (single-enry muliple-exit region) we maintain a counter of
// executions. If a trace has been executed more than a certain threshold, we
// start skipping this trace sometimes.
// The LiteRace paper suggests several strategies for sampling, including
// thread-local counters. Having thread local counters for all threads is too
// expensive, so we have 8 arrays of counters and use the array (tid % 8).
//
// TODO(kcc): this currently does not work with --keep-history=0
//
// Note: ANNOTATE_PUBLISH_MEMORY() does not work with sampling... :(


static const size_t n_literace_counters = 1024 * 1024;
static const size_t n_literace_threads = 8;
static uint32_t literace_counters[n_literace_threads][n_literace_counters];


#ifdef _MSC_VER
#include <intrin.h>
inline unsigned u32_log2(unsigned x) {
  unsigned long y;
  _BitScanReverse(&y, x);
  return y;
}
#endif

#ifdef __GNUC__
inline unsigned u32_log2(unsigned x) {
  return 31 - __builtin_clz(x);
}
#endif



bool LiteRaceSkipTrace(int tid, uint32_t trace_no, uint32_t sampling_rate) {
  DCHECK(u32_log2(1) == 0);
  DCHECK(u32_log2(1 << 4) == 4);
  DCHECK(u32_log2(1 << 30) == 30);

  if (sampling_rate == 0) return false;

  // sampling_rate indicates the level of sampling.
  // 0 means no sampling.
  // 1 means handle *almost* all accesses.
  // ...
  // 31 means very aggressive sampling (skip a lot of accesses).

  trace_no %= n_literace_counters;

  uint32_t counter = ++literace_counters[tid % n_literace_threads][trace_no];
  CHECK(sampling_rate < 32);
  int shift = 32 - sampling_rate;
  DCHECK(shift > 1);
  int high_bits = counter >> shift;
  if (high_bits) {  // counter is big enough.
    int n_high_bits = u32_log2(high_bits);
    uint32_t mask = (1U << n_high_bits) - 1;
    // The higher the value of the counter, the bigger the probability that we
    // will skip this trace.
    if ((counter & mask) != 0) {
      return true;
    }
  }
  return false;
}

// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
