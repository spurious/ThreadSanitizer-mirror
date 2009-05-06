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

#include <ext/hash_map>
#include <set>
#include <vector>
#include <execinfo.h>
#include <stdlib.h>
#include <pthread.h>


#include <assert.h>
#ifdef NDEBUG
# error "Pleeease, do not define NDEBUG"
#endif 
#define CHECK assert

class Mutex {
 public: 
  Mutex() {
    CHECK(0 == pthread_mutex_init(&mu_, NULL));
  }
  ~Mutex() {
    CHECK(0 == pthread_mutex_destroy(&mu_));
  }
  void Lock()    { CHECK(0 == pthread_mutex_lock(&mu_));}
  void Unlock()  { CHECK(0 == pthread_mutex_unlock(&mu_)); }
 private:
  pthread_mutex_t mu_;
};


static int race_checker_level = 
  getenv("RACECHECKER") ? atoi(getenv("RACECHECKER")) : 0;
static int race_checker_sleep_ms = 
  getenv("RACECHECKER_SLEEP_MS") ? atoi(getenv("RACECHECKER_SLEEP_MS")) : 1;


struct CallSite {         // Data about a call site.
  pthread_t thread;
  int nstack;
  void *stack[20];
};

struct TypedCallsites {
  std::vector<CallSite> type[2];    // Index 0 is for reads, index 1 is for writes.
};

typedef __gnu_cxx::hash_map<uintptr_t, TypedCallsites> AddressMap;

static Mutex race_checker_mu;
static AddressMap *race_checker_map;               // Under race_checker_mu.


// Return a string decribing the callsites of the threads
// accessing a location.
static void DescribeAccesses(TypedCallsites *c) {
  fprintf(stderr, "Race found between these points\n");
  std::set<pthread_t> reported_accessors;
  for (int t = 1; t >= 0; t--) {  // Iterate starting from writers.
    for (size_t i = 0; i != c->type[t].size(); i++) {
      CallSite *s = &c->type[t][i];
      if (reported_accessors.insert(s->thread).second) {
        // Report each accessor just once.
        fprintf(stderr, "%s\n", (t == 0? "=== reader: " : "=== writer: "));
        // msg += (s->thread);
        backtrace_symbols_fd(s->stack+2, s->nstack-2, 2/*stderr*/);
        // msg += util::SymbolizeStackTraceAsString(s->stack, s->nstack);
      }
    }
  }
}

// Record an access of type "type" by the calling thread to "address".
// type is 0 for reads or 1 or for writes.
// address addresses a variable on which a race is suspected.
void RaceChecker::Start(RaceChecker::Type type, const volatile void *address) {
  if (race_checker_level > 0) {
    this->type_ = type;
    this->address_ = reinterpret_cast<uintptr_t>(address);
    this->thread_ = pthread_self();
    CallSite callsite;
    callsite.nstack = 
        backtrace(callsite.stack,
                  sizeof(callsite.stack)/sizeof(callsite.stack[0]));
    callsite.thread = this->thread_;
    race_checker_mu.Lock();
    if (race_checker_map == 0) {
      race_checker_map = new AddressMap;
    }
    TypedCallsites *c = &(*race_checker_map)[this->address_];
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
        DescribeAccesses(c);
        if (race_checker_level >= 2) {
          exit(1);
        }
      }
    }
    race_checker_mu.Unlock();
    if (race_checker_sleep_ms != 0) {
      usleep(race_checker_sleep_ms * 1000);
    }
  }
}

// Remove the access recorded by this->Start(). Assumes that a single thread
// call Start/End in a correctly-nested fashion on a single address.
void RaceChecker::End() {
  if (race_checker_level > 0) {
    race_checker_mu.Lock();
    TypedCallsites *c = &(*race_checker_map)[this->address_];
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
      race_checker_map->erase(this->address_);
    }
    race_checker_mu.Unlock();
  }
}
