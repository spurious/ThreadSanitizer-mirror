/*
  Copyright (C) 2008-2008 Google Inc
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

// Implementation of RaceChecker class, see race_checker.h.
// Author: Konstantin Serebryany

#include "race_checker.h"

#include <map>
#include <set>
#include <vector>

#include <stdlib.h>

#ifdef _MSC_VER
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <pthread.h>
#endif

#include <assert.h>
#ifdef NDEBUG
# error "Pleeease, do not define NDEBUG"
#endif
#define CHECK assert

#ifdef _MSC_VER
class Mutex {
 public:
  Mutex()       { ::InitializeCriticalSection(&cs_); }
  ~Mutex()      { ::DeleteCriticalSection(&cs_); }
  void Lock()   { ::EnterCriticalSection(&cs_); }
  void Unlock() { ::LeaveCriticalSection(&cs_); }
 private:
  CRITICAL_SECTION cs_;
};

BOOL symtable_initialized = SymInitialize(GetCurrentProcess(), NULL, TRUE);

int ReadIntFromEnv(const char* s, int default_value) {
  char tmp[1024] = "?";
  if (!::GetEnvironmentVariableA(s, tmp, sizeof(tmp)))
    return default_value;
  return atoi(tmp);
}

#else
class Mutex {
 public:
  Mutex()        { pthread_mutex_init(&mu_, NULL); }
  ~Mutex()       { pthread_mutex_destroy(&mu_); }
  void Lock()    { pthread_mutex_lock(&mu_); }
  void Unlock()  { pthread_mutex_unlock(&mu_); }
 private:
  pthread_mutex_t mu_;
};

int ReadIntFromEnv(const char* s, int default_value) {
  return getenv(s) ? atoi(getenv(s)) : default_value;
}
#endif

static int race_checker_level     = ReadIntFromEnv("RACECHECKER", 0);
static int race_checker_sleep_ms  = ReadIntFromEnv("RACECHECKER_SLEEP_MS", 1);
static int race_checker_verbosity = ReadIntFromEnv("RACECHECKER_VERBOSITY", 0);

struct CallSite {         // Data about a call site.
  RaceChecker::ThreadId thread;
  int nstack;
  void *stack[20];
};

struct TypedCallsites {
  std::vector<CallSite> type[2];    // Index 0 is for reads, index 1 is for writes.
};

typedef std::map<std::string, TypedCallsites> AddressMap;

static Mutex race_checker_mu;
static AddressMap *race_checker_map;  // Under race_checker_mu.

// Return a string decribing the callsites of the threads
// accessing a location.
static void DescribeAccesses(std::string id, TypedCallsites *c) {
  fprintf(stderr, "Race on '%s' found between these points\n", id.c_str());
  std::set<RaceChecker::ThreadId> reported_accessors;
  for (int t = 1; t >= 0; t--) {  // Iterate starting from writers.
    for (size_t i = 0; i != c->type[t].size(); i++) {
      CallSite *s = &c->type[t][i];
      if (reported_accessors.insert(s->thread).second) {
        // Report each accessor just once.
        fprintf(stderr, "%s\n", (t == 0? "=== reader: " : "=== writer: "));
      #ifdef _MSC_VER
        // From http://msdn.microsoft.com/en-us/library/ms680578(VS.85).aspx
        for (int i = 2; i < s->nstack; i++) {
          DWORD frame = (DWORD)s->stack[i];
          ULONG64 buffer[(sizeof(SYMBOL_INFO) +
                         MAX_SYM_NAME * sizeof(TCHAR) +
                         sizeof(ULONG64) - 1) /
                         sizeof(ULONG64)];
          PSYMBOL_INFO pSymbol = (PSYMBOL_INFO) buffer;

          pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          pSymbol->MaxNameLen = MAX_SYM_NAME - 1;

          if (!SymFromAddr(GetCurrentProcess(), frame,
                           NULL, pSymbol)) {
            fprintf(stderr, "[0x%X]\n", frame);
            continue;
          }

          IMAGEHLP_LINE64 line;
          SymSetOptions(SYMOPT_LOAD_LINES);
          line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
          DWORD line_disp = 0;

          if (SymGetLineFromAddr64(GetCurrentProcess(), frame, &line_disp, &line))
            fprintf(stderr, "[0x%X] <%s:%i>\n", frame, pSymbol->Name, line.LineNumber);
          else
            fprintf(stderr, "[0x%X] <%s>\n", frame, pSymbol->Name);
        }
      #else
        backtrace_symbols_fd(s->stack+2, s->nstack-2, 2/*stderr*/);
      #endif
      }
    }
  }
}

// Record an access of type "type_" by the calling thread to "address_".
// type_ is 0 for reads or 1 or for writes.
// address_ addresses a variable on which a race is suspected.
void RaceChecker::Start() {
  if (race_checker_level <= 0 || IdIsEmpty())
    return;

#ifdef _MSC_VER
  this->thread_ = GetCurrentThreadId();
#else
  this->thread_ = pthread_self();
#endif
  if (race_checker_verbosity > 0) {
    fprintf(stderr, "RaceChecker::%s instance created for '%s' on thread 0x%X\n",
            this->type_ == WRITE ? "WRITE" : "READ ",
            this->id_.c_str(), (unsigned int)this->thread_);
  }
  CallSite callsite;
  callsite.nstack =
#ifdef _MSC_VER
      CaptureStackBackTrace(0,
                sizeof(callsite.stack)/sizeof(callsite.stack[0]),
                callsite.stack, NULL);
#else
      backtrace(callsite.stack,
                sizeof(callsite.stack)/sizeof(callsite.stack[0]));
#endif
  callsite.thread = this->thread_;
  race_checker_mu.Lock();
  if (race_checker_map == 0) {
    race_checker_map = new AddressMap;
  }
  TypedCallsites *c = &(*race_checker_map)[this->id_];
  c->type[this->type_].push_back(callsite);
  // A race requires at least one writer and at least two accessors.
  if (c->type[WRITE].size() != 0 &&
      c->type[READ].size() + c->type[WRITE].size() > 1) {
    // Race only if a writer is a different thread from another accessor.
    bool is_race = false;
    for (size_t w = 0; !is_race && w != c->type[WRITE].size(); w++) {
      for (int t = 0; !is_race && t != 2; t++) {
        for (size_t i = 0; !is_race && i != c->type[t].size(); i++) {
          is_race = (c->type[WRITE][w].thread != c->type[t][i].thread);
        }
      }
    }
    if (is_race) {
      DescribeAccesses(this->id_, c);
      if (race_checker_level >= 2) {
        exit(1);
      }
    }
  }
  race_checker_mu.Unlock();
  if (race_checker_sleep_ms != 0) {
    #ifdef _MSC_VER
    Sleep(race_checker_sleep_ms);
    #else
    usleep(race_checker_sleep_ms * 1000);
    #endif
  }
}

// Remove the access recorded by this->Start(). Assumes that a single thread
// call Start/End in a correctly-nested fashion on a single address.
void RaceChecker::End() {
  if (race_checker_level <= 0 || IdIsEmpty())
    return;

  race_checker_mu.Lock();
  TypedCallsites *c = &(*race_checker_map)[this->id_];
  std::vector<CallSite> &vec = c->type[this->type_];
  int i;
  for (i = vec.size() - 1; i >= 0; --i) {
    if (vec[i].thread == this->thread_) {
      vec.erase(vec.begin() + i);
      break;
    }
  }
  CHECK(i >= 0);
  if (c->type[READ].size() + c->type[WRITE].size() == 0) {
    race_checker_map->erase(this->id_);
  }
  race_checker_mu.Unlock();
}
