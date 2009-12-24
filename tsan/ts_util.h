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

//--------- Head ------------------- {{{1
#include <glob.h>
#include <stdint.h>


#ifdef TS_VALGRIND
# include "ts_valgrind.h"
# define CHECK tl_assert
#else
# undef NDEBUG  // Assert is always on.
# include <assert.h>
# define CHECK assert
#endif // TS_NO_VALGRIND

//--------- STL ------------------- {{{1
#ifdef TS_USE_STANDARD_STL
#include <string.h>
#include <limits.h>
#include <set>
#include <map>
#include <vector>
#include <deque>
#include <stack>
#include <algorithm>
#include <string>
#include <bitset>
//#include <algorithm>
#include "ext/algorithm"
#include "ext/hash_map"
#include "ext/hash_set"
using __gnu_cxx::hash_map;
using __gnu_cxx::hash_set;
using __gnu_cxx::lexicographical_compare_3way;

#else  // no TS_USE_STANDARD_STL

#include "stlport/set"
#include "stlport/map"
#include "stlport/hash_map"
#include "stlport/hash_set"
#include "stlport/vector"
#include "stlport/deque"
#include "stlport/stack"
#include "stlport/algorithm"
#include "stlport/string"
#include "stlport/bitset"
#include "stlport/algorithm"

using std::hash_map;
using std::hash_set;
using std::lexicographical_compare_3way;
#endif  // TS_USE_STANDARD_STL


using std::set;
using std::multiset;
using std::multimap;
using std::map;
using std::deque;
using std::stack;
using std::string;
using std::vector;
using std::bitset;

using std::min;
using std::max;
using std::sort;
using std::pair;
using std::make_pair;
using std::unique_copy;

//--------- defines ------------------- {{{1

#ifdef TS_VALGRIND
// TODO(kcc) get rid of these macros.
#define sprintf(arg1, arg2...) VG_(sprintf)((Char*)arg1, (HChar*)arg2)
#define vsnprintf(a1, a2, a3, a4) VG_(vsnprintf)((Char*)a1, a2, a3, a4)
#define getpid VG_(getpid)
#define strchr(a,b)    VG_(strchr)((Char*)a,b)
#define strdup(a) (char*)VG_(strdup)((HChar*)"strdup", (const Char*)a)
#define snprintf(a,b,c...)     VG_(snprintf)((Char*)a,b,c)
#define read VG_(read)
#define getenv(x) VG_(getenv)((Char*)x)
#define close VG_(close)
#define write VG_(write)
#define usleep(a) /*nothing. TODO.*/

#else // No TS_VALGRIND
#include <unistd.h>

#define UNIMPLEMENTED() CHECK(0 == 42)
#define UNLIKELY(x) __builtin_expect((x), 0)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#endif // TS_VALGRIND

#define CHECK_GT(X, Y) CHECK((X) >  (Y))
#define CHECK_LT(X, Y) CHECK((X) < (Y))
#define CHECK_GE(X, Y) CHECK((X) >= (Y))
#define CHECK_LE(X, Y) CHECK((X) <= (Y))
#define CHECK_NE(X, Y) CHECK((X) != (Y))
#define CHECK_EQ(X, Y) CHECK((X) == (Y))

#if defined(DEBUG) && DEBUG >= 1
  #define DCHECK(a) CHECK(a)
  #define DEBUG_MODE (1)
  #define INLINE
#else
  #define DCHECK(a) do { if (0) { if (a) {} } } while(0)
  #define DEBUG_MODE (0)
  #define INLINE  inline  __attribute__ ((always_inline))
#endif

#define NOINLINE __attribute__ ((noinline))

//--------- Malloc profiling ------------------- {{{1
void PushMallocCostCenter(const char *cc);
void PopMallocCostCenter();

class ScopedMallocCostCenter {
 public:
  ScopedMallocCostCenter(const char *cc) {
#if defined(DEBUG) && defined(TS_VALGRIND)
      PushMallocCostCenter(cc);
#endif
  }
  ~ScopedMallocCostCenter() {
#if defined(DEBUG) && defined(TS_VALGRIND)
      PopMallocCostCenter();
#endif
  }
};

//--------- Forward decls ------------------- {{{1
class ThreadSanitizerReport;

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

extern uintptr_t GetPcOfCurrentThread();

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
