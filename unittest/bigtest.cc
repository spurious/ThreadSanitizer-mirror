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
   long nMutex_Lock_Unlock; // number of Mutex64 (Lock-Unlock)s
   // TODO: cache lines?
   long nBytes_Excl;    // number of bytes accessed from only one thread
   long nBytes_NonExcl; // number of bytes accessed from more than one thread
   long nRaceyAccesses; // number of data races in the test
   long nTotalMemoryAccesses;
};

struct Test{
   typedef void (*void_func_void_t)(void);
   
   /* may return false to indicate smth like "Wait didn't succeed" */
   typedef bool (*bool_func_void_t)(void);
   //typedef TestStats (*TestStats_func_void_t)(void);
   
   //TestStats_func_void_t GetStats_;
   void_func_void_t Run_v_;
   bool_func_void_t Run_b_;
   
   Test() : /*GetStats_(0), */Run_v_(0), Run_b_(0) {}
   Test(int id, /*TestStats_func_void_t _GetStats, */void_func_void_t _Run) 
      : //GetStats_(_GetStats),
        Run_v_(_Run), Run_b_(0) {}
   Test(int id, /*TestStats_func_void_t _GetStats, */bool_func_void_t _Run) 
      : //GetStats_(_GetStats),
        Run_v_(0), Run_b_(_Run) {}
   /*TestStats GetStats() {
      return GetStats_();
   }*/
   bool Run() {
      if (Run_v_ == NULL) {
         CHECK(Run_b_ != NULL);
         return (*Run_b_)();
      } else {
         Run_v_();
         return true;
      }
   }
};

typedef std::map<int, Test> MapOfTests;
MapOfTests the_map_of_tests;

struct TestAdder {
   TestAdder(int id, //Test::TestStats_func_void_t _GetStats, 
                     Test::void_func_void_t _Run)
   {
      CHECK(the_map_of_tests.count(id) == 0);
      the_map_of_tests[id] = Test(id, /*_GetStats,*/ _Run);
   }
   TestAdder(int id, //Test::TestStats_func_void_t _GetStats, 
                     Test::bool_func_void_t _Run)
   {
      CHECK(the_map_of_tests.count(id) == 0);
      the_map_of_tests[id] = Test(id, /*_GetStats,*/ _Run);
   }
};

#define REGISTER_PATTERN(id) TestAdder add_test##id(id, Pattern##id)

ThreadPool * mainThreadPool;

// Print everything under a mutex
Mutex printf_mu;
#define printf(args...) \
    do{ \
      printf_mu.Lock();\
      fprintf(stdout, args);\
      printf_mu.Unlock(); \
    }while(0)

// Accessing memory locations holding one lock {{{1
namespace one_lock {
   // TODO: make these constants as parameters
   const int NUM_CONTEXTS = 16;
   const int DATA_SIZE = 128;
   const int NUM_ITERATIONS = 16;
   
   struct TestContext {
      Mutex64 MU;
      char data[DATA_SIZE];
   } contexts[NUM_CONTEXTS];
   
   // Write accesses
   void Pattern101() {    
      printf("Pattern101\n");  
      int id = rand() % NUM_CONTEXTS;
      TestContext * context = &contexts[id];
      for (int i = 0; i < NUM_ITERATIONS; i++) {
         context->MU.Lock();
            for (int j = 0; j < DATA_SIZE; j++) {
               context->data[j] = 77; // write
            }
         context->MU.Unlock();
      }
   }
   REGISTER_PATTERN(101);
   
   // Read accesses
   void Pattern102() {
      printf("Pattern102\n");
      int id = rand() % NUM_CONTEXTS;
      TestContext * context = &contexts[id];
      for (int i = 0; i < NUM_ITERATIONS; i++) {
         int temp = 0;
         context->MU.Lock();
            for (int j = 0; j < DATA_SIZE; j++) {
               temp += context->data[j]; // read
            }
         context->MU.Unlock();
      }
   }
   REGISTER_PATTERN(102);
   
   int atomic_integers[NUM_CONTEXTS] = {0};
   // Atomic increment
   void Pattern103() {
      printf("Pattern103\n");
      int id = rand() % NUM_CONTEXTS;
      for (int i = 0; i < NUM_ITERATIONS; i++)
         __sync_add_and_fetch(&atomic_integers[id], 1);
   }
   REGISTER_PATTERN(103);
} // namespace one_lock

// Accessing memory locations holding random LockSets {{{1
namespace multiple_locks {
   // TODO: make these constants as parameters
   const int NUM_CONTEXTS = 16;
   const int DATA_SIZE = 128;
   const int NUM_ITERATIONS = 16;
      
   struct TestContext {
      Mutex64 MU;
      char data[DATA_SIZE];
   } contexts[NUM_CONTEXTS];

   // Access random context holding a random LS including context->MU
   void Pattern201() {
      printf("Pattern201\n");
      TestContext * context = &contexts[rand() % NUM_CONTEXTS]; 
      std::vector<Mutex64*> LS;
      // STL nightmare starts here - calculate random LS{{{1
      {
         std::vector<int> tmp_LS;
         for (int i = 0; i < NUM_CONTEXTS; i++)
            tmp_LS.push_back(i);
         std::random_shuffle(tmp_LS.begin(), tmp_LS.end());
         
         // TODO: #LS as a parameter
         for (int i = 0; i < NUM_CONTEXTS/4; i++)
            LS.push_back(&contexts[tmp_LS[i]].MU);
         
         // This LS should contain context's Mutex to have proper synchronization
         LS.push_back(&context->MU);         
         
         // LS should be sorted to avoid deadlocks
         std::sort(LS.begin(), LS.end());
         
         // LS should not contain context->MU twice
         std::vector<Mutex64*>::iterator new_end = std::unique(LS.begin(), LS.end());
         LS.erase(new_end, LS.end());
      } // end of STL nightmare :-)
      
      for (int i = 0; i < NUM_ITERATIONS; i++) {
         for (std::vector<Mutex64*>::iterator it = LS.begin(); it != LS.end(); it++)
            (*it)->Lock();
         for (int j = 0; j < DATA_SIZE; j++)
            context->data[j] = 77;
         for (std::vector<Mutex64*>::iterator it = LS.begin(); it != LS.end(); it++)
            (*it)->Unlock();
      }
   }
   REGISTER_PATTERN(201);
} // namespace multiple_locks

// Publishing objects using different synchronization patterns {{{1
namespace publishing {
   namespace pcq {
      const int NUM_CONTEXTS = 16;
   
      struct TestContext {
         ProducerConsumerQueue pcq;
         
         TestContext() : pcq(0) {}
         ~TestContext() {
            void * ptr = NULL;
            // Erase the contents of the PCQ. We assume NULL can't be there
            pcq.Put(NULL);
            while(ptr = pcq.Get())
               delete [] (char*)ptr;
         }
      } contexts[NUM_CONTEXTS];
   
      // Publish a random string into a random PCQ
      void Pattern301() {
         printf("Pattern301\n");
         TestContext * context = &contexts[rand() % NUM_CONTEXTS];
         // TODO: str_len as a parameter
         int str_len = 1 + (rand() % 255);
         char * str = new char[str_len + 1];
         CHECK(str != NULL);
         for (int i = 0; i < str_len; i++)
            str[i] = 'a';
         str[str_len] = '\0';
         context->pcq.Put(str);
      }
      REGISTER_PATTERN(301);
      
      // Read a published string from a random PCQ. MAYFAIL!
      bool Pattern302() {
         printf("Pattern302\n");
         TestContext * context = &contexts[rand() % NUM_CONTEXTS];
         char * str = NULL;
         if (context->pcq.TryGet((void**)&str)) {
            int tmp = strlen(str);
            delete [] str;
            return true;
         }
         return false;
      }
      REGISTER_PATTERN(302);
   }
} // namespace publishing

// Threads work with their memory exclusively
namespace thread_local {
   // Thread accesses heap
   void Pattern401() {
      printf("Pattern401\n");
      // TODO: parameters
      const int DATA_SIZE  = 1024;
      const int ITERATIONS = 16;
      
      char * temp = new char[DATA_SIZE + 1];
      for (int i = 1; i <= ITERATIONS; i++) {
         memset(temp, i, DATA_SIZE);
         temp[DATA_SIZE] = 0;
         int size = strlen(temp);
      }
      delete [] temp;
   }
   REGISTER_PATTERN(401);
   
   // Thread accesses stack
   void Pattern402() {
      printf("Pattern402\n");
      // TODO: parameters
      const int DATA_SIZE  = 1024;
      const int ITERATIONS = 16;
      
      char temp[DATA_SIZE];
      for (int i = 1; i <= ITERATIONS; i++) {
         memset(temp, i, DATA_SIZE);
         temp[DATA_SIZE] = 0;
         int size = strlen(temp);
      }
   }
   REGISTER_PATTERN(402);
} // namespace thread_local

// Different benign races scenarios
namespace benign_races {
   namespace stats {
      int simple_counter = 0;
      
      int odd_counter = 1;
      Mutex64 odd_counter_mu;
      
      struct __ {
         __() {
            ANNOTATE_BENIGN_RACE(&simple_counter, "Pattern501");
         }
      } _;
   
      void Pattern501() {
         simple_counter++;
      }
      REGISTER_PATTERN(501);
      
      // increment odd_counter, but first check it is >0 (double-check) 
      void Pattern502() {
         printf("Pattern502\n");
         if (ANNOTATE_UNPROTECTED_READ(odd_counter) > 0) {
            odd_counter_mu.Lock();
            if (odd_counter > 0)
               odd_counter++;
            odd_counter_mu.Unlock();
         }
      }
      REGISTER_PATTERN(502);
   }
      
} // namespace benign_races

void PatternDispatcher() {
   std::vector<int> availablePatterns;
   for (MapOfTests::iterator it = the_map_of_tests.begin();
         it != the_map_of_tests.end(); it++) {
      availablePatterns.push_back(it->first);
   }
   for (int i = 0; i < 16; i++) {
      //int idx = rand() % availablePatterns.size();
      int idx = i % availablePatterns.size();      
      int test_idx = availablePatterns[idx];
      // TODO: the above code should be replaced with a proper randomizer
      // with a "specify distribution function" feature
      the_map_of_tests[test_idx].Run();
   }
}

int main () {
   const int N = 3;
   mainThreadPool = new ThreadPool(N);
   mainThreadPool->StartWorkers();
   for (int i = 0; i < N; i++) {
      mainThreadPool->Add(NewCallback(PatternDispatcher));
   }
   delete mainThreadPool;
   
   return 0;
}
