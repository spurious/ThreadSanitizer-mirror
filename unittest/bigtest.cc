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

class Mutex64: public Mutex {
   // force sizeof(Mutex64) >= 64
private:
   char ___[(sizeof(Mutex) > 64) ? (0) : (64 - sizeof(Mutex))];
};

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
   //typedef TestStats (*TestStats_func_void_t)(void);
   
   //TestStats_func_void_t GetStats_;
   void_func_void_t Run_;
   
   Test() : /*GetStats_(0), */Run_(0) {}
   Test(int id, /*TestStats_func_void_t _GetStats, */void_func_void_t _Run) 
   : //GetStats_(_GetStats),
     Run_(_Run)
   {}
   /*TestStats GetStats() {
      return GetStats_();
   }*/
   void Run() {
      Run_();
   }
};

std::map<int, Test> TheMapOfTests;

struct TestAdder {
   TestAdder(int id, //Test::TestStats_func_void_t _GetStats, 
                     Test::void_func_void_t _Run)
   {
      CHECK(TheMapOfTests.count(id) == 0);
      TheMapOfTests[id] = Test(id, /*_GetStats,*/ _Run);
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

#define REGISTER_TEST(id) TestAdder add_test(id, Run)

ThreadPool * mainThreadPool;

// Simple test that does nothing {{{1
namespace test00 {   
   void Run() {}
   
   REGISTER_TEST(00);
} // namespace test00

// 2 threads access the same memory location holding one common lock {{{1
namespace test01 {
   const int NUM_CONTEXTS = 16;
   const int NUM_ITERATIONS = 16;
   const int DATA_SIZE = 128;
   
   struct TestContext {
      Mutex MU;
      char data[DATA_SIZE];
   } contexts[NUM_CONTEXTS];

   void Worker(TestContext * context) {
      Mutex * MU = &context->MU;
      for (int i = 0; i < NUM_ITERATIONS; i++) {
         MU->Lock();
         for (int j = 0; j < DATA_SIZE; j++)
            context->data[j] = 77;
         MU->Unlock();
      }
   }

   void Run() {
      int id = rand() % NUM_CONTEXTS;
      for (int i = 0; i < 2; i++)
         mainThreadPool->Add(NewCallback(Worker, &contexts[id]));
   }
   
   REGISTER_TEST(01);
} // namespace test01

// 2 threads access the same memory location holding different LS {{{1
namespace test02 {
   const int NUM_CONTEXTS = 16;
   const int DATA_SIZE = 128;
   const int NUM_ITERATIONS = 16;
      
   struct TestContext {
      Mutex MU;
      char data[DATA_SIZE];
   } contexts[NUM_CONTEXTS];

   void Worker(TestContext * context) {
      std::vector<Mutex*> LS;
      // STL nightmare here {{{1
      {
         std::vector<int> tmp_LS;
         for (int i = 0; i < NUM_CONTEXTS; i++)
            tmp_LS.push_back(i);
         std::random_shuffle(tmp_LS.begin(), tmp_LS.end());
         
         for (int i = 0; i < NUM_CONTEXTS/4; i++)
            LS.push_back(&contexts[tmp_LS[i]].MU);
         LS.push_back(&context->MU);
         std::sort(LS.begin(), LS.end());
         std::vector<Mutex*>::iterator new_end = std::unique(LS.begin(), LS.end());
         LS.erase(new_end, LS.end());
         /*std::string ls = "LS: ";
         for (std::vector<Mutex*>::iterator it = LS.begin(); it != LS.end(); it++) {
            char temp[128];
            sprintf(temp, "0x%X ", *it);
            ls += temp;
         } 
         ls += "\n";
      printf(ls.c_str());*/
      } // end of STL nightmare :-)
      
      for (int i = 0; i < NUM_ITERATIONS; i++) {
         for (std::vector<Mutex*>::iterator it = LS.begin(); it != LS.end(); it++)
            (*it)->Lock();
         for (int j = 0; j < DATA_SIZE; j++)
            context->data[j] = 77;
         for (std::vector<Mutex*>::iterator it = LS.begin(); it != LS.end(); it++)
            (*it)->Unlock();
      }
   }

   void Run() {
      int id = rand() % NUM_CONTEXTS;
      for (int i = 0; i < 2; i++)
         mainThreadPool->Add(NewCallback(Worker, &contexts[id]));
   }
   
   REGISTER_TEST(02);
} // namespace test02

// T1 publishes a memory location to T2 using Signal-Wait {{{1
namespace test03 {
   const int DATA_SIZE = 1024;
   
   struct TestContext {
      Mutex MU;
      CondVar CV;
      char * data;
      TestContext () : data(NULL) {}
   };

   void Signaller(TestContext * context) {
      char * temp = new char[DATA_SIZE]; 
      for (int i = 0; i < DATA_SIZE; i++)
         temp[i] = 77;

      Mutex   * MU = &context->MU;
      CondVar * CV = &context->CV;
      MU->Lock();
         context->data = temp;
         CV->Signal();
      MU->Unlock();
   }

   void Waiter(TestContext * context) {
      Mutex   * MU = &context->MU;
      CondVar * CV = &context->CV;
      MU->Lock();
      while (context->data == NULL)
         CV->Wait(MU);
      ANNOTATE_CONDVAR_LOCK_WAIT(CV, MU);         
      MU->Unlock();
      
      for (int i = 0; i < DATA_SIZE; i++)
         CHECK(77 == context->data[i]);
      
      delete [] context->data;
   }
   
   void Run() {
      TestContext * tc = new TestContext();
      mainThreadPool->Add(NewCallback(Signaller, tc));
      mainThreadPool->Add(NewCallback(Waiter, tc));
   }
   
   REGISTER_TEST(03);
} // namespace test03

int main () {
   mainThreadPool = new ThreadPool(3);
   mainThreadPool->StartWorkers();
   test01::Run();
   test02::Run();
   test03::Run();
   delete mainThreadPool;
   
   return 0;
}
