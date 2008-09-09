/*
  This file is part of Valgrind, a dynamic binary instrumentation
  framework.

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

// Author: Timur Iskhodzhanov <opensource@google.com> 
//
// This file contains a set of benchmarks for data race detection tools.

#include <vector>
#include <string>
#include <map>
#include <ext/hash_map>
#include <algorithm>
#include <cstring>      // strlen(), index(), rindex()
#include <ctime>

#include "thread_wrappers_pthread.h"

struct TestStats {
   // Contains information about what resources does specific test utilize and
   // how much
   long nThreads;
   long nCV;         // number of CondVar's used by the test
   long nCV_Signals; // number of CondVar->Signal's
   long nCV_Waits;   // number of CondVar->Wait's
   long nMutexes;    // number of Mutexes used by the test
   long nMutex_Lock_Unlock; // number of Mutex (Lock-Unlock)s
   // TODO: cache lines?
   long nBytes_Excl;    // number of bytes accessed from only one thread
   long nBytes_NonExcl; // number of bytes accessed from more than one thread
   long nRaceyAccesses; // number of data races in the test
   long nTotalMemoryAccesses;
};

struct Test{
   typedef void (*void_func_void_t)(void);
   typedef TestStats (*TestStats_func_void_t)(void);
   
   TestStats_func_void_t GetStats_;
   void_func_void_t Run_;
   
   Test() : GetStats_(0), Run_(0) {}
   Test(int id, TestStats_func_void_t _GetStats, void_func_void_t _Run) 
   : GetStats_(_GetStats)
   , Run_(_Run)
   {}
   TestStats GetStats() {
      return GetStats_();
   }
   void Run() {
      Run_();
   }
};

std::map<int, Test> TheMapOfTests;

struct TestAdder {
   TestAdder(int id, Test::TestStats_func_void_t _GetStats, 
                     Test::void_func_void_t _Run)
   {
      CHECK(TheMapOfTests.count(id) == 0);
      TheMapOfTests[id] = Test(id, _GetStats, _Run);
   }
};

class MyThreadArray {
 public:
  typedef void (*F) (void);
  MyThreadArray(F f1, F f2 = NULL, F f3 = NULL, F f4 = NULL) {
    ar_[0] = new MyThread(f1);
    ar_[1] = f2 ? new MyThread(f2) : NULL;
    ar_[2] = f3 ? new MyThread(f3) : NULL;
    ar_[3] = f4 ? new MyThread(f4) : NULL;
  }
  void Start() {
    for(int i = 0; i < 4; i++) {
      if(ar_[i]) {
        ar_[i]->Start();
        usleep(10);
      }
    }
  }

  void Join() {
    for(int i = 0; i < 4; i++) {
      if(ar_[i]) {
        ar_[i]->Join();
      }
    }
  }

  ~MyThreadArray() {
    for(int i = 0; i < 4; i++) {
      delete ar_[i];
    }
  }
 private:
  MyThread *ar_[4];
};

#define REGISTER_TEST(id) TestAdder add_test(id, GetStats, Run)

// Simple test that does nothing {{{1
namespace test00 {   
   TestStats GetStats() {
      TestStats ts;
      memset(&ts, 0, sizeof(ts));
      return ts;
   }
   
   void Run() {}
   
   REGISTER_TEST(00);
} // namespace test00

int main () {
   return 0;
}
