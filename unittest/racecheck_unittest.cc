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

// Author: Konstantin Serebryany <opensource@google.com> 
//
// This file contains a set of unit tests for a data race detection tool. 
//
//
//
// This test can be compiled with pthreads (default) or
// with any other library that supports threads, locks, cond vars, etc. 
// 
// To compile with pthreads: 
//   g++  racecheck_unittest.cc dynamic_annotations.cc 
//        -lpthread -g -DDYNAMIC_ANNOTATIONS=1
// 
// To compile with different library: 
//   1. cp thread_wrappers_pthread.h thread_wrappers_yourlib.h
//   2. edit thread_wrappers_yourlib.h
//   3. add '-DTHREAD_WRAPPERS="thread_wrappers_yourlib.h"' to your compilation.
//
//

// This test must not include any other file specific to threading library,
// everything should be inside THREAD_WRAPPERS. 
#ifndef THREAD_WRAPPERS 
# define THREAD_WRAPPERS "thread_wrappers_pthread.h"
#endif 
#include THREAD_WRAPPERS

// Helgrind memory usage testing stuff
// If not present in dynamic_annotations.h/.cc - ignore
#ifndef ANNOTATE_RESET_STATS
#define ANNOTATE_RESET_STATS()
#endif
#ifndef ANNOTATE_PRINT_STATS
#define ANNOTATE_PRINT_STATS()
#endif
#ifndef ANNOTATE_PRINT_MEMORY_USAGE
#define ANNOTATE_PRINT_MEMORY_USAGE(a)
#endif
//

#include <vector>
#include <string>
#include <map>
#include <ext/hash_map>
#include <algorithm>
#include <cstring>      // strlen(), index(), rindex()
#include <ctime>

// The tests are
// - Stability tests (marked STAB)
// - Performance tests (marked PERF)
// - Feature tests
//   - TN (true negative) : no race exists and the tool is silent. 
//   - TP (true positive) : a race exists and reported. 
//   - FN (false negative): a race exists but not reported. 
//   - FP (false positive): no race exists but the tool reports it. 
//
// The feature tests are marked according to the behavior of helgrind 3.3.0.
//
// TP and FP tests are annotated with ANNOTATE_EXPECT_RACE, 
// so, no error reports should be seen when running under helgrind. 
//
// When some of the FP cases are fixed in helgrind we'll need 
// to update this test.
//
// Each test resides in its own namespace. 
// Namespaces are named test01, test02, ... 
// Please, *DO NOT* change the logic of existing tests nor rename them. 
// Create a new test instead. 
//
// Some tests use sleep()/usleep(). 
// This is not a synchronization, but a simple way to trigger 
// some specific behaviour of the race detector's scheduler.

// Globals and utilities used by several tests. {{{1
Mutex   MU, MU1, MU2; 
CondVar CV; 
int     COND = 0;


typedef void (*void_func_void_t)(void);
enum TEST_FLAG {
  FEATURE           = 1 << 0, 
  STABILITY         = 1 << 1, 
  PERFORMANCE       = 1 << 2,
  EXCLUDE_FROM_ALL  = 1 << 3,
  NEEDS_ANNOTATIONS = 1 << 4,
  RACE_DEMO         = 1 << 5,
  MEMORY_USAGE      = 1 << 6,
  PRINT_STATS       = 1 << 7
};

// Put everything into stderr.
Mutex printf_mu;
#define printf(args...) \
    do{ \
      printf_mu.Lock();\
      fprintf(stderr, args);\
      printf_mu.Unlock(); \
    }while(0)

struct Test{
  void_func_void_t f_;
  int flags_;
  Test(void_func_void_t f, int flags) 
    : f_(f)
    , flags_(flags)
  {}
  Test() : f_(0), flags_(0) {}
  void Run() {
     ANNOTATE_RESET_STATS();
     if (flags_ & PERFORMANCE) {
        clock_t start = clock();
        f_();
        clock_t end   = clock();
        printf ("Time: %4dms\n", (int)((end-start)/(CLOCKS_PER_SEC/1000)));
     } else
        f_();
     if (flags_ & PRINT_STATS)
        ANNOTATE_PRINT_STATS();
     if (flags_ & MEMORY_USAGE)
        ANNOTATE_PRINT_MEMORY_USAGE(0);
  }
};
std::map<int, Test> TheMapOfTests;


struct TestAdder {
  TestAdder(void_func_void_t f, int id, int flags = FEATURE) {
    CHECK(TheMapOfTests.count(id) == 0);
    TheMapOfTests[id] = Test(f, flags);
  }
};

#define REGISTER_TEST(f, id)         TestAdder add_test_##id (f, id);
#define REGISTER_TEST2(f, id, flags) TestAdder add_test_##id (f, id, flags);

static bool ArgIsOne(int *arg) { return *arg == 1; };
static bool ArgIsZero(int *arg) { return *arg == 0; };
static bool ArgIsTrue(bool *arg) { return *arg == true; };

// Call ANNOTATE_EXPECT_RACE only if 'machine' env variable is defined. 
// Useful to test against several different machines. 
#define ANNOTATE_EXPECT_RACE_FOR_MACHINE(mem, descr, machine) \
    while(getenv(machine)) {\
      ANNOTATE_EXPECT_RACE(mem, descr); \
      break;\
    }\

#ifndef MAIN_INIT_ACTION
#define MAIN_INIT_ACTION
#endif 

int main(int argc, char** argv) { // {{{1
  MAIN_INIT_ACTION;
  if (argc == 2 && !strcmp(argv[1], "benchmark")) {
     for (std::map<int,Test>::iterator it = TheMapOfTests.begin(); 
         it != TheMapOfTests.end();
         ++it) {
       if(!(it->second.flags_ & PERFORMANCE)) continue;
       it->second.Run();
     }      
  } else if (argc > 1) {     
    // the tests are listed in command line flags 
    for (int i = 1; i < argc; i++) {
      int f_num = atoi(argv[i]);
      CHECK(TheMapOfTests.count(f_num));
      TheMapOfTests[f_num].Run();
    }
  } else {
    bool run_tests_with_annotations = false;
    if (getenv("DRT_ALLOW_ANNOTATIONS")) {
      run_tests_with_annotations = true;
    }
    for (std::map<int,Test>::iterator it = TheMapOfTests.begin(); 
        it != TheMapOfTests.end();
        ++it) {
      if(it->second.flags_ & EXCLUDE_FROM_ALL) continue;
      if(it->second.flags_ & RACE_DEMO) continue;
      if((it->second.flags_ & NEEDS_ANNOTATIONS)
         && run_tests_with_annotations == false) continue;
      it->second.Run();
    } 
  }
}

// An array of threads. Create/start/join all elements at once. {{{1
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



// test00: {{{1
namespace test00 {
int     GLOB = 0;
void Run() {
  printf("test00: negative\n");
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 00)
}  // namespace test00


// test01: TP. Simple race (write vs write). {{{1
namespace test01 {
int     GLOB = 0;
void Worker() {
  GLOB = 1; 
}

void Parent() {
  MyThread t(Worker);
  t.Start();
//  ThreadPool pool(1);
//  pool.StartWorkers();
//  pool.Add(NewCallback(Worker));
//  usleep(100000);
  GLOB = 2;
  t.Join();
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test01. TP.");
  printf("test01: positive\n");
  Parent();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 1);
}  // namespace test01


// test02: TN. Synchronization via CondVar. {{{1
namespace test02 {
int     GLOB = 0;
// Two write accesses to GLOB are synchronized because 
// the pair of CV.Signal() and CV.Wait() establish happens-before relation. 
//
// Waiter:                      Waker: 
// 1. COND = 0
// 2. Start(Waker)              
// 3. MU.Lock()                 a. write(GLOB)
//                              b. MU.Lock()
//                              c. COND = 1
//                         /--- d. CV.Signal()
//  4. while(COND)        /     e. MU.Unock()
//       CV.Wait(MU) <---/
//  5. MU.Unlock()
//  6. write(GLOB)

void Waker() {
  usleep(10000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();
}

void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  MU.Unlock();
  GLOB = 2;
}
void Run() {
  printf("test02: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 2);
}  // namespace test02


// test03: TN. Synchronization via LockWhen, signaller gets there first. {{{1
namespace test03 {  
int     GLOB = 0;
// Two write accesses to GLOB are synchronized via conditional critical section. 
// Note that LockWhen() happens first (we use sleep(1) to make sure)! 
//
// Waiter:                           Waker: 
// 1. COND = 0
// 2. Start(Waker)              
//                                   a. write(GLOB)
//                                   b. MU.Lock()
//                                   c. COND = 1
//                              /--- d. MU.Unlock()
// 3. MU.LockWhen(COND==1) <---/     
// 4. MU.Unlock()
// 5. write(GLOB)

void Waker() {
  usleep(100000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  MU.Unlock(); // calls ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  MU.LockWhen(Condition(&ArgIsOne, &COND));  // calls ANNOTATE_CONDVAR_WAIT
  MU.Unlock();  // Waker is done! 

  GLOB = 2;
}
void Run() {
  printf("test03: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 3, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test03

// test04: TN. Synchronization via PCQ. {{{1
namespace test04 {
int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX); 
// Two write accesses to GLOB are separated by PCQ Put/Get. 
//
// Putter:                        Getter:
// 1. write(GLOB)                
// 2. Q.Put() ---------\          .
//                      \-------> a. Q.Get()
//                                b. write(GLOB)


void Putter() {
  GLOB = 1; 
  Q.Put(NULL);
}

void Getter() {
  Q.Get();
  GLOB = 2;
}

void Run() {
  printf("test04: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start(); 
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 4);
}  // namespace test04


// test05: FP. Synchronization via CondVar, but waiter does not block. {{{1
// Since CondVar::Wait() is not called, we get a false positive. 
namespace test05 {
int     GLOB = 0;
// Two write accesses to GLOB are synchronized via CondVar. 
// But race detector can not see it. 
// See this for details: 
// http://www.valgrind.org/docs/manual/hg-manual.html#hg-manual.effective-use. 
//
// Waiter:                                  Waker: 
// 1. COND = 0                         
// 2. Start(Waker)                          
// 3. MU.Lock()                             a. write(GLOB)
//                                          b. MU.Lock()
//                                          c. COND = 1
//                                          d. CV.Signal()
//  4. while(COND)                          e. MU.Unock()
//       CV.Wait(MU) <<< not called   
//  5. MU.Unlock()      
//  6. write(GLOB)      

void Waker() {
  GLOB = 1; 
  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();
}

void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  usleep(100000);  // Make sure the signaller gets first.
  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  MU.Unlock();
  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test05. FP. Unavoidable.");
  printf("test05: unavoidable false positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 5);
}  // namespace test05


// test06: TN. Synchronization via CondVar, but Waker gets there first.  {{{1
namespace test06 {
int     GLOB = 0;
// Same as test05 but we annotated the Wait() loop. 
//
// Waiter:                                            Waker: 
// 1. COND = 0                                   
// 2. Start(Waker)                                    
// 3. MU.Lock()                                       a. write(GLOB)
//                                                    b. MU.Lock()
//                                                    c. COND = 1
//                                           /------- d. CV.Signal()
//  4. while(COND)                          /         e. MU.Unock()
//       CV.Wait(MU) <<< not called        /
//  6. ANNOTATE_CONDVAR_WAIT(CV, MU) <----/
//  5. MU.Unlock()      
//  6. write(GLOB)      

void Waker() {
  GLOB = 1; 
  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();
}

void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  usleep(100000);  // Make sure the signaller gets first.
  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);

  MU.Unlock();
  GLOB = 2;
}
void Run() {
  printf("test06: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 6, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test06


// test07: TN. Synchronization via LockWhen() but Waker gets there first. {{{1
namespace test07 {  
int     GLOB = 0;
// Two write accesses to GLOB are synchronized via conditional critical section. 
// Note that LockWhen() happens after COND has been set (due to sleep)! 
// We have to annotate Waker with ANNOTATE_CONDVAR_SIGNAL(), otherwise 
// ANNOTATE_CONDVAR_WAIT() will succeed w/o signal. 
//
// Waiter:                           Waker: 
// 1. COND = 0
// 2. Start(Waker)              
//                                   a. write(GLOB)
//                                   b. MU.Lock()
//                                   c. COND = 1
//                              /--- d. ANNOTATE_CONDVAR_SIGNAL(&MU); 
// 3. MU.LockWhen(COND==1) <---/     e. MU.Unlock()
// 4. MU.Unlock()
// 5. write(GLOB)

void Waker() {
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  ANNOTATE_CONDVAR_SIGNAL(&MU);
  MU.Unlock(); // does not call ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  usleep(100000);  // Make sure the signaller gets there first.

  MU.LockWhen(Condition(&ArgIsOne, &COND));  // calls ANNOTATE_CONDVAR_WAIT
  MU.Unlock();  // Waker is done! 

  GLOB = 2;
}
void Run() {
  printf("test07: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 7, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test07

// test08: TN. Synchronization via thread start/join. {{{1
namespace test08 {
int     GLOB = 0;
// Three accesses to GLOB are separated by thread start/join. 
//
// Parent:                        Worker:
// 1. write(GLOB)
// 2. Start(Worker) ------------>
//                                a. write(GLOB)
// 3. Join(Worker) <------------
// 4. write(GLOB)
void Worker() {
  GLOB = 2; 
}

void Parent() {
  MyThread t(Worker);
  GLOB = 1;
  t.Start();
  t.Join();
  GLOB = 3;
}
void Run() {
  printf("test08: negative\n");
  Parent();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 8);
}  // namespace test08


// test09: TP. Simple race (read vs write). {{{1
namespace test09 {
int     GLOB = 0;
// A simple data race between writer and reader. 
// Write happens after read (enforced by sleep). 
// Usually, easily detectable by a race detector. 
void Writer() {
  usleep(100000);
  GLOB = 3; 
}
void Reader() {
  CHECK(GLOB != -777);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test09. TP.");
  printf("test09: positive\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 9);
}  // namespace test09


// test10: FN. Simple race (write vs read). {{{1
namespace test10 {
int     GLOB = 0;
// A simple data race between writer and reader. 
// Write happens before Read (enforced by sleep), 
// otherwise this test is the same as test09. 
// 
// Writer:                    Reader:
// 1. write(GLOB)             a. sleep(long enough so that GLOB 
//                                is most likely initialized by Writer)
//                            b. read(GLOB)
// 
//
// Eraser algorithm does not detect the race here, 
// see Section 2.2 of http://citeseer.ist.psu.edu/savage97eraser.html. 
//
void Writer() {
  GLOB = 3; 
}
void Reader() {
  usleep(100000);
  CHECK(GLOB != -777);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test10. FN in MSMHelgrind.");
  printf("test10: positive\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 10);
}  // namespace test10


// test11: FP. Synchronization via CondVar, 2 workers. {{{1
// This test is properly synchronized, but currently (Dec 2007) 
// helgrind reports a false positive. 
//
// Parent:                              Worker1, Worker2: 
// 1. Start(workers)                    a. read(GLOB)
// 2. MU.Lock()                         b. MU.Lock()
// 3. while(COND != 2)        /-------- c. CV.Signal()
//      CV.Wait(&MU) <-------/          d. MU.Unlock()
// 4. MU.Unlock()
// 5. write(GLOB) 
//
namespace test11 {
int     GLOB = 0;
void Worker() {
  usleep(100000);
  CHECK(GLOB != 777); 

  MU.Lock();
  COND++;
  CV.Signal();
  MU.Unlock();
}

void Parent() {
  COND = 0;

  MyThreadArray t(Worker, Worker);
  t.Start();

  MU.Lock();
  while(COND != 2) {
    CV.Wait(&MU);
  }
  MU.Unlock();

  GLOB = 2;

  t.Join();
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test11. FP. Fixed by MSMProp1.");
  printf("test11: negative\n");
  Parent();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 11);
}  // namespace test11


// test12: FP. Synchronization via Mutex, then via PCQ. {{{1
namespace test12 {
int     GLOB = 0;
// This test is properly synchronized, but currently (Dec 2007) 
// helgrind reports a false positive. 
//
// First, we write to GLOB under MU, then we synchronize via PCQ, 
// which is essentially a semaphore. 
//
// Putter:                       Getter:
// 1. MU.Lock()                  a. MU.Lock()
// 2. write(GLOB) <---- MU ----> b. write(GLOB)
// 3. MU.Unlock()                c. MU.Unlock()
// 4. Q.Put()   ---------------> d. Q.Get()
//                               e. write(GLOB)
                               
ProducerConsumerQueue Q(INT_MAX);

void Putter() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  Q.Put(NULL);
}

void Getter() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  Q.Get();
  GLOB++;
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test12. FP. Fixed by MSMProp1.");
  printf("test12: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 12);
}  // namespace test12


// test13: FP. Synchronization via Mutex, then via LockWhen. {{{1
namespace test13 { 
int     GLOB = 0;
// This test is essentially the same as test12, but uses LockWhen 
// instead of PCQ.
//
// Waker:                                     Waiter:
// 1. MU.Lock()                               a. MU.Lock()
// 2. write(GLOB) <---------- MU ---------->  b. write(GLOB)
// 3. MU.Unlock()                             c. MU.Unlock()
// 4. MU.Lock()                               .
// 5. COND = 1                                .
// 6. ANNOTATE_CONDVAR_SIGNAL -------\        .        
// 7. MU.Unlock()                     \       .
//                                     \----> d. MU.LockWhen(COND == 1)
//                                            e. MU.Unlock()
//                                            f. write(GLOB)
void Waker() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  MU.Lock();
  COND = 1;
  ANNOTATE_CONDVAR_SIGNAL(&MU);
  MU.Unlock();
}

void Waiter() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  MU.LockWhen(Condition(&ArgIsOne, &COND));
  MU.Unlock();
  GLOB++;
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test13. FP. Fixed by MSMProp1.");
  printf("test13: negative\n");
  COND = 0;

  MyThreadArray t(Waker, Waiter);
  t.Start();
  t.Join();

  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 13, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test13


// test14: FP. Synchronization via PCQ, reads, 2 workers. {{{1
namespace test14 {
int     GLOB = 0;
// This test is properly synchronized, but currently (Dec 2007) 
// helgrind reports a false positive. 
//
// This test is similar to test11, but uses PCQ (semaphore). 
//
// Putter2:                  Putter1:                     Getter: 
// 1. read(GLOB)             a. read(GLOB)
// 2. Q2.Put() ----\         b. Q1.Put() -----\           .
//                  \                          \--------> A. Q1.Get()
//                   \----------------------------------> B. Q2.Get()
//                                                        C. write(GLOB)
ProducerConsumerQueue Q1(INT_MAX), Q2(INT_MAX);

void Putter1() {
  CHECK(GLOB != 777);
  Q1.Put(NULL);
}
void Putter2() {
  CHECK(GLOB != 777);
  Q2.Put(NULL);
}
void Getter() {
  Q1.Get();
  Q2.Get(); 
  GLOB++;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test14. FP. Fixed by MSMProp1.");
  printf("test14: negative\n");
  MyThreadArray t(Getter, Putter1, Putter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 14);
}  // namespace test14


// test15: TN. Synchronization via LockWhen. One waker and 2 waiters. {{{1
namespace test15 {
// Waker:                                   Waiter1, Waiter2:
// 1. write(GLOB)
// 2. MU.Lock()
// 3. COND = 1
// 4. ANNOTATE_CONDVAR_SIGNAL ------------> a. MU.LockWhen(COND == 1)
// 5. MU.Unlock()                           b. MU.Unlock()
//                                          c. read(GLOB)

int     GLOB = 0;

void Waker() {
  GLOB = 2;

  MU.Lock();
  COND = 1;
  ANNOTATE_CONDVAR_SIGNAL(&MU);
  MU.Unlock();
};

void Waiter() {
  MU.LockWhen(Condition(&ArgIsOne, &COND));
  MU.Unlock();
  CHECK(GLOB != 777);
}


void Run() {
  COND = 0;
  printf("test15: negative\n");
  MyThreadArray t(Waker, Waiter, Waiter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 15);
}  // namespace test15


// test16: FP. Barrier (emulated by CV), 2 threads. {{{1
namespace test16 {
// Worker1:                                     Worker2:
// 1. MU.Lock()                                 a. MU.Lock()
// 2. write(GLOB) <------------ MU ---------->  b. write(GLOB)
// 3. MU.Unlock()                               c. MU.Unlock()
// 4. MU2.Lock()                                d. MU2.Lock()
// 5. COND--                                    e. COND--
// 6. ANNOTATE_CONDVAR_SIGNAL(MU2) >>>>>V       .
// 7. MU2.Await(COND == 0) <------------+------ f. ANNOTATE_CONDVAR_SIGNAL(MU2)
// 8. MU2.Unlock()                      V>>>>>> g. MU2.Await(COND == 0)
// 9. read(GLOB)                                h. MU2.Unlock()
//                                              i. read(GLOB)
//
//
// TODO: This way we may create too many edges in happens-before graph. 
// Arndt Mühlenfeld in his PhD (TODO: link) suggests creating special nodes in 
// happens-before graph to reduce the total number of edges. 
// See figure 3.14. 
//
//
int     GLOB = 0;
Mutex MU2; 

void Worker() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  MU2.Lock(); 
  COND--;
  ANNOTATE_CONDVAR_SIGNAL(&MU2);
  MU2.Await(Condition(&ArgIsZero, &COND));
  MU2.Unlock();

  CHECK(GLOB == 2);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test16. FP. Fixed by MSMProp1 + Barrier support.");
  COND = 2;
  printf("test16: negative\n");
  MyThreadArray t(Worker, Worker);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 16, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test16


// test17: FP. Barrier (emulated by CV), 3 threads. {{{1
namespace test17 {
// Same as test16, but with 3 threads.
int     GLOB = 0;
Mutex MU2; 

void Worker() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();

  MU2.Lock(); 
  COND--;
  ANNOTATE_CONDVAR_SIGNAL(&MU2);
  MU2.Await(Condition(&ArgIsZero, &COND));
  MU2.Unlock();

  CHECK(GLOB == 3);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test17. FP. Fixed by MSMProp1 + Barrier support.");
  COND = 3;
  printf("test17: negative\n");
  MyThreadArray t(Worker, Worker, Worker);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 17, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test17


// test18: TN. Synchronization via Await(), signaller gets there first. {{{1
namespace test18 {  
int     GLOB = 0;
// Same as test03, but uses Mutex::Await() instead of Mutex::LockWhen(). 

void Waker() {
  usleep(100000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  MU.Unlock(); // calls ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));

  MU.Lock();
  MU.Await(Condition(&ArgIsOne, &COND));  // calls ANNOTATE_CONDVAR_WAIT
  MU.Unlock();  // Waker is done! 

  GLOB = 2;
}
void Run() {
  printf("test18: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 18, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test18

// test19: TN. Synchronization via AwaitWithTimeout(). {{{1
namespace test19 {  
int     GLOB = 0;
// Same as test18, but with AwaitWithTimeout. Do not timeout. 
void Waker() {
  usleep(100000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  MU.Unlock(); // calls ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));

  MU.Lock();
  CHECK(MU.AwaitWithTimeout(Condition(&ArgIsOne, &COND), INT_MAX));
  MU.Unlock();

  GLOB = 2;
}
void Run() {
  printf("test19: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 19, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test19

// test20: TP. Incorrect synchronization via AwaitWhen(), timeout. {{{1
namespace test20 {  
int     GLOB = 0;
// True race. We timeout in AwaitWhen.
void Waker() {
  GLOB = 1; 
  usleep(100 * 1000);
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));

  MU.Lock();
  CHECK(!MU.AwaitWithTimeout(Condition(&ArgIsOne, &COND), 100));
  MU.Unlock();

  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test20. TP.");
  printf("test20: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 20, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test20

// test21: TP. Incorrect synchronization via LockWhenWithTimeout(). {{{1
namespace test21 {  
int     GLOB = 0;
// True race. We timeout in LockWhenWithTimeout().
void Waker() {
  GLOB = 1; 
  usleep(100 * 1000);
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));

  CHECK(!MU.LockWhenWithTimeout(Condition(&ArgIsOne, &COND), 100));
  MU.Unlock();

  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test21. TP.");
  printf("test21: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 21, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test21

// test22: TP. Incorrect synchronization via CondVar::WaitWithTimeout(). {{{1
namespace test22 {  
int     GLOB = 0;
// True race. We timeout in CondVar::WaitWithTimeout().
void Waker() {
  GLOB = 1; 
  usleep(100 * 1000);
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));

  int64_t ms_left_to_wait = 100; 
  int64_t deadline_ms = GetCurrentTimeMillis() + ms_left_to_wait;
  MU.Lock();
  while(COND != 1 && ms_left_to_wait > 0) {
    CV.WaitWithTimeout(&MU, ms_left_to_wait);
    ms_left_to_wait = deadline_ms - GetCurrentTimeMillis();
  }
  MU.Unlock();

  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test22. TP.");
  printf("test22: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 22);
}  // namespace test22

// test23: TN. TryLock, ReaderLock, ReaderTryLock. {{{1
namespace test23 {  
// Correct synchronization with TryLock, Lock, ReaderTryLock, ReaderLock. 
int     GLOB = 0;
void Worker_TryLock() {
  for (int i = 0; i < 20; i++) {
    while (true) {
      if (MU.TryLock()) {
        GLOB++; 
        MU.Unlock();
        break;
      }
      usleep(1000);
    }
  }
}

void Worker_ReaderTryLock() {
  for (int i = 0; i < 20; i++) {
    while (true) {
      if (MU.ReaderTryLock()) {
        CHECK(GLOB != 777); 
        MU.ReaderUnlock();
        break;
      }
      usleep(1000);
    }
  }
}

void Worker_ReaderLock() {
  for (int i = 0; i < 20; i++) {
    MU.ReaderLock();
    CHECK(GLOB != 777); 
    MU.ReaderUnlock();
    usleep(1000);
  }
}

void Worker_Lock() {
  for (int i = 0; i < 20; i++) {
    MU.Lock();
    GLOB++;
    MU.Unlock();
    usleep(1000);
  }
}

void Run() {
  printf("test23: negative\n");
  MyThreadArray t(Worker_TryLock, 
                  Worker_ReaderTryLock, 
                  Worker_ReaderLock,
                  Worker_Lock
                  );
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 23);
}  // namespace test23

// test24: TN. Synchronization via ReaderLockWhen(). {{{1
namespace test24 {  
int     GLOB = 0;
// Same as test03, but uses ReaderLockWhen(). 

void Waker() {
  usleep(100000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  MU.Unlock(); // calls ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  MU.ReaderLockWhen(Condition(&ArgIsOne, &COND));
  MU.ReaderUnlock();

  GLOB = 2;
}
void Run() {
  printf("test24: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 24, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test24

// test25: TN. Synchronization via ReaderLockWhenWithTimeout(). {{{1
namespace test25 {  
int     GLOB = 0;
// Same as test24, but uses ReaderLockWhenWithTimeout(). 
// We do not timeout. 

void Waker() {
  usleep(100000);  // Make sure the waiter blocks.
  GLOB = 1; 

  MU.Lock();
  COND = 1; // We are done! Tell the Waiter. 
  MU.Unlock(); // calls ANNOTATE_CONDVAR_SIGNAL;
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  CHECK(MU.ReaderLockWhenWithTimeout(Condition(&ArgIsOne, &COND), INT_MAX));
  MU.ReaderUnlock();

  GLOB = 2;
}
void Run() {
  printf("test25: negative\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 25, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test25

// test26: TP. Incorrect synchronization via ReaderLockWhenWithTimeout(). {{{1
namespace test26 {  
int     GLOB = 0;
// Same as test25, but we timeout and incorrectly assume happens-before. 

void Waker() {
  GLOB = 1; 
  usleep(10000);
}
void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
  CHECK(!MU.ReaderLockWhenWithTimeout(Condition(&ArgIsOne, &COND), 100));
  MU.ReaderUnlock();

  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test26. TP");
  printf("test26: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 26, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test26


// test27: TN. Simple synchronization via SpinLock. {{{1
namespace test27 {
#ifndef NO_SPINLOCK
int     GLOB = 0;
SpinLock MU;
void Worker() {
  MU.Lock();
  GLOB++; 
  MU.Unlock();
  usleep(10000);
}

void Run() {
  printf("test27: negative\n");
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 27, FEATURE|NEEDS_ANNOTATIONS);
#endif // NO_SPINLOCK
}  // namespace test27


// test28: FP. Synchronization via Mutex, then PCQ. 3 threads {{{1
namespace test28 {
// Putter1:                       Getter:                         Putter2:        
// 1. MU.Lock()                                                   A. MU.Lock()
// 2. write(GLOB)                                                 B. write(GLOB)
// 3. MU.Unlock()                                                 C. MU.Unlock()
// 4. Q.Put() ---------\                                 /------- D. Q.Put()
// 5. MU.Lock()         \-------> a. Q.Get()            /         E. MU.Lock()
// 6. read(GLOB)                  b. Q.Get() <---------/          F. read(GLOB)
// 7. MU.Unlock()                   (sleep)                       G. MU.Unlock()
//                                c. read(GLOB)
ProducerConsumerQueue Q(INT_MAX);
int     GLOB = 0;

void Putter() {
  MU.Lock();
  GLOB++;
  MU.Unlock();

  Q.Put(NULL);

  MU.Lock();
  CHECK(GLOB != 777);
  MU.Unlock();
}

void Getter() {
  Q.Get();
  Q.Get();
  usleep(100000);
  CHECK(GLOB == 2);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test28. FP.");
  printf("test28: negative\n");
  MyThreadArray t(Getter, Putter, Putter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 28);
}  // namespace test28


// test29: FP. Synchronization via Mutex, then PCQ. 4 threads. {{{1
namespace test29 {
// Similar to test28, but has two Getters and two PCQs. 
ProducerConsumerQueue *Q1, *Q2;
int     GLOB = 0;

void Putter(ProducerConsumerQueue *q) {
  MU.Lock();
  GLOB++;
  MU.Unlock();

  q->Put(NULL);
  q->Put(NULL);

  MU.Lock();
  CHECK(GLOB != 777);
  MU.Unlock();

}

void Putter1() { Putter(Q1); }
void Putter2() { Putter(Q2); }

void Getter() {
  Q1->Get();
  Q2->Get();
  usleep(100000);
  CHECK(GLOB == 2);
  usleep(48000); //  TODO: remove this when FP in test32 is fixed. 
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test29. FP.");
  printf("test29: negative\n");
  Q1 = new ProducerConsumerQueue(INT_MAX);
  Q2 = new ProducerConsumerQueue(INT_MAX);
  MyThreadArray t(Getter, Getter, Putter1, Putter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
  delete Q1;
  delete Q2;
}
REGISTER_TEST(Run, 29);
}  // namespace test29


// test30: TN. Synchronization via 'safe' race. Writer vs multiple Readers. {{{1
namespace test30 {
// This test shows a very risky kind of synchronization which is very easy 
// to get wrong. Actually, I am not sure I've got it right. 
//
// Writer:                                 Reader1, Reader2, ..., ReaderN: 
// 1. write(GLOB[i]: i >= BOUNDARY)        a. n = BOUNDARY
// 2. ANNOTATE_SIGNAL(BOUNDARY+1) -------> b. ANNOTATE_WAIT(n)
// 3. BOUNDARY++;                          c. read(GLOB[i]: i < n)
//
// Here we have a 'safe' race on accesses to BOUNDARY and 
// no actual races on accesses to GLOB[]: 
// Writer writes to GLOB[i] where i>=BOUNDARY and then increments BOUNDARY. 
// Readers read BOUNDARY and read GLOB[i] where i<BOUNDARY. 
//
// I am not completely sure that this scheme guaranties no race between 
// accesses to GLOB since compilers and CPUs 
// are free to rearrange memory operations. 
// I am actually sure that this scheme is wrong unless we use 
// some smart memory fencing... 
//
// For this unit test we use ANNOTATE_CONDVAR_WAIT/ANNOTATE_CONDVAR_SIGNAL 
// but for real life we will need separate annotations 
// (if we ever want to annotate this synchronization scheme at all). 


const int N = 48;
static int GLOB[N];
volatile int BOUNDARY = 0;

void Writer() {
  for (int i = 0; i < N; i++) {
    CHECK(BOUNDARY == i);
    for (int j = i; j < N; j++) {
      GLOB[j] = j;
    }
    ANNOTATE_CONDVAR_SIGNAL(reinterpret_cast<void*>(BOUNDARY+1));
    BOUNDARY++;
    usleep(1000);
  }
}

void Reader() {
  int n;
  do {
    n = BOUNDARY;
    if (n == 0) continue; 
    ANNOTATE_CONDVAR_WAIT(reinterpret_cast<void*>(n));
    for (int i = 0; i < n; i++) {
      CHECK(GLOB[i] == i);
    }
    usleep(100);
  } while(n < N);
}

void Run() {
  ANNOTATE_EXPECT_RACE((void*)(&BOUNDARY), "test30. Sync via 'safe' race.");
  printf("test30: negative\n");
  MyThreadArray t(Writer, Reader, Reader, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB[N-1]);
}
REGISTER_TEST2(Run, 30, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test30


// test31: TN. Synchronization via 'safe' race. Writer vs Writer. {{{1
namespace test31 {
// This test is similar to test30, but 
// it has one Writer instead of mulitple Readers. 
//
// Writer1:                                Writer2 
// 1. write(GLOB[i]: i >= BOUNDARY)        a. n = BOUNDARY
// 2. ANNOTATE_SIGNAL(BOUNDARY+1) -------> b. ANNOTATE_WAIT(n)
// 3. BOUNDARY++;                          c. write(GLOB[i]: i < n)
//

const int N = 48;
static int GLOB[N];
volatile int BOUNDARY = 0;

void Writer1() {
  for (int i = 0; i < N; i++) {
    CHECK(BOUNDARY == i);
    for (int j = i; j < N; j++) {
      GLOB[j] = j;
    }
    ANNOTATE_CONDVAR_SIGNAL(reinterpret_cast<void*>(BOUNDARY+1));
    BOUNDARY++;
    usleep(1000);
  }
}

void Writer2() {
  int n;
  do {
    n = BOUNDARY;
    if (n == 0) continue; 
    ANNOTATE_CONDVAR_WAIT(reinterpret_cast<void*>(n));
    for (int i = 0; i < n; i++) {
      if(GLOB[i] == i) {
        GLOB[i]++;
      }
    }
    usleep(100);
  } while(n < N);
}

void Run() {
  ANNOTATE_EXPECT_RACE((void*)(&BOUNDARY), "test31. Sync via 'safe' race.");
  printf("test31: negative\n");
  MyThreadArray t(Writer1, Writer2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB[N-1]);
}
REGISTER_TEST2(Run, 31, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test31


// test32: FP. Synchronization via thread create/join. W/R. {{{1
namespace test32 {
// This test is well synchronized but helgrind 3.3.0 reports a race. 
//
// Parent:                   Writer:               Reader:  
// 1. Start(Reader) -----------------------\       .
//                                          \      .
// 2. Start(Writer) ---\                     \     .
//                      \---> a. MU.Lock()    \--> A. sleep(long enough)
//                            b. write(GLOB)     
//                      /---- c. MU.Unlock()
// 3. Join(Writer) <---/                           
//                                                 B. MU.Lock()
//                                                 C. read(GLOB)
//                                   /------------ D. MU.Unlock()
// 4. Join(Reader) <----------------/
// 5. write(GLOB)
//
//
// The call to sleep() in Reader is not part of synchronization, 
// it is required to trigger the false positive in helgrind 3.3.0. 
//
int     GLOB = 0;

void Writer() {
  MU.Lock();
  GLOB = 1;
  MU.Unlock();
}

void Reader() {
  usleep(480000);
  MU.Lock();
  CHECK(GLOB != 777);
  MU.Unlock();
}

void Parent() {
  MyThread r(Reader);
  MyThread w(Writer);
  r.Start(); 
  w.Start();

  w.Join();  // 'w' joins first. 
  r.Join(); 

  GLOB = 2;
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test32. FP. Fixed by MSMProp1.");
  printf("test32: negative\n");
  Parent();
  printf("\tGLOB=%d\n", GLOB);
}

REGISTER_TEST(Run, 32);
}  // namespace test32


// test33: STAB. Stress test for the number of thread sets (TSETs). {{{1
namespace test33 {
int     GLOB = 0;
// Here we access N memory locations from within log(N) threads. 
// We do it in such a way that helgrind creates nearly all possible TSETs. 
// Then we join all threads and start again (N_iter times). 
const int N_iter = 48;
const int Nlog  = 15;
const int N     = 1 << Nlog;
static int ARR[N];

void Worker() {
  MU.Lock();
  int n = ++GLOB;
  MU.Unlock();

  n %= Nlog;
  for (int i = 0; i < N; i++) {
    // ARR[i] is accessed by threads from i-th subset 
    if (i & (1 << n)) {
        CHECK(ARR[i] == 0);
    }
  }
}

void Run() {
  printf("test33:\n");

  std::vector<MyThread*> vec(Nlog);

  for (int i = 0; i < N_iter; i++) {
    // Create and start Nlog threads
    for (int i = 0; i < Nlog; i++) {
      vec[i] = new MyThread(Worker);
    }
    for (int i = 0; i < Nlog; i++) {
      vec[i]->Start();
    }
    // Join all threads. 
    for (int i = 0; i < Nlog; i++) {
      vec[i]->Join();
      delete vec[i];
    }
    printf("------------------\n");
  }

  printf("\tGLOB=%d; ARR[1]=%d; ARR[7]=%d; ARR[N-1]=%d\n", 
         GLOB, ARR[1], ARR[7], ARR[N-1]);
}
REGISTER_TEST2(Run, 33, STABILITY|EXCLUDE_FROM_ALL);
}  // namespace test33


// test34: STAB. Stress test for the number of locks sets (LSETs). {{{1
namespace test34 {
// Similar to test33, but for lock sets. 
int     GLOB = 0;
const int N_iter = 48;
const int Nlog = 10;
const int N    = 1 << Nlog;
static int ARR[N];
static Mutex *MUs[Nlog];

void Worker() {
    for (int i = 0; i < N; i++) {
      // ARR[i] is protected by MUs from i-th subset of all MUs
      for (int j = 0; j < Nlog; j++)  if (i & (1 << j)) MUs[j]->Lock();
      CHECK(ARR[i] == 0);
      for (int j = 0; j < Nlog; j++)  if (i & (1 << j)) MUs[j]->Unlock();
    }
}

void Run() {
  printf("test34:\n");
  for (int iter = 0; iter < N_iter; iter++) {
    for (int i = 0; i < Nlog; i++) {
      MUs[i] = new Mutex;
    }
    MyThreadArray t(Worker, Worker);
    t.Start();
    t.Join();
    for (int i = 0; i < Nlog; i++) {
      delete MUs[i];
    }
    printf("------------------\n");
  }
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 34, STABILITY|EXCLUDE_FROM_ALL);
}  // namespace test34


// test35: PERF. Lots of mutexes and lots of call to free().  {{{1
namespace test35 {
// Helgrind 3.3.0 has very slow in shadow_mem_make_NoAccess(). Fixed locally.
// With the fix helgrind runs this test about a minute.  
// Without the fix -- about 5 minutes. (on c2d 2.4GHz). 
//
// TODO: need to figure out the best way for performance testing. 
int **ARR; 
const int N_mu   = 10000;
const int N_free = 480000;

void Worker() {
  for (int i = 0; i < N_free; i++) 
    CHECK(777 == *ARR[i]);
}

void Run() {
  printf("test35:\n");
  std::vector<Mutex*> mus;

  ARR = new int *[N_free];
  for (int i = 0; i < N_free; i++) {
    const int c = N_free / N_mu;
    if ((i % c) == 0) {
      mus.push_back(new Mutex);
      mus.back()->Lock();
      mus.back()->Unlock();
    }
    ARR[i] = new int(777);
  }

  // Need to put all ARR[i] into shared state in order 
  // to trigger the performance bug. 
  MyThreadArray t(Worker, Worker);
  t.Start();
  t.Join();
  
  for (int i = 0; i < N_free; i++) delete ARR[i];
  delete [] ARR;
  
  for (int i = 0; i < mus.size(); i++) {
    delete mus[i];
  }
}
REGISTER_TEST2(Run, 35, PERFORMANCE|EXCLUDE_FROM_ALL);
}  // namespace test35


// test36: FP. Synchronization via Mutex, then PCQ. 3 threads. W/W {{{1
namespace test36 {
// variation of test28 (W/W instead of W/R) 

// Putter1:                       Getter:                         Putter2:        
// 1. MU.Lock();                                                  A. MU.Lock()
// 2. write(GLOB)                                                 B. write(GLOB)
// 3. MU.Unlock()                                                 C. MU.Unlock()
// 4. Q.Put() ---------\                                 /------- D. Q.Put()
// 5. MU1.Lock()        \-------> a. Q.Get()            /         E. MU1.Lock()  
// 6. MU.Lock()                   b. Q.Get() <---------/          F. MU.Lock()   
// 7. write(GLOB)                                                 G. write(GLOB) 
// 8. MU.Unlock()                                                 H. MU.Unlock() 
// 9. MU1.Unlock()                  (sleep)                       I. MU1.Unlock()
//                                c. MU1.Lock()   
//                                d. write(GLOB)  
//                                e. MU1.Unlock() 
ProducerConsumerQueue Q(INT_MAX);
int     GLOB = 0;

void Putter() {
  MU.Lock();
  GLOB++;
  MU.Unlock();

  Q.Put(NULL);

  MU1.Lock();
  MU.Lock();
  GLOB++;
  MU.Unlock();
  MU1.Unlock();
}

void Getter() {
  Q.Get();
  Q.Get();
  usleep(100000);
  MU1.Lock();
  GLOB++;
  MU1.Unlock();
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test36. FP.");
  printf("test36: negative \n");
  MyThreadArray t(Getter, Putter, Putter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 36);
}  // namespace test36


// test37: TN. Simple synchronization (write vs read). {{{1
namespace test37 {
int     GLOB = 0;
// Similar to test10, but properly locked. 
// Writer:             Reader: 
// 1. MU.Lock()      
// 2. write
// 3. MU.Unlock()                   
//                    a. MU.Lock()
//                    b. read
//                    c. MU.Unlock();              

void Writer() {
  MU.Lock();
  GLOB = 3; 
  MU.Unlock();
}
void Reader() {
  usleep(100000);
  MU.Lock();
  CHECK(GLOB != -777);
  MU.Unlock();
}

void Run() {
  printf("test37: negative\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 37);
}  // namespace test37


// test38: FP. Synchronization via Mutexes and PCQ. 4 threads. W/W {{{1
namespace test38 {
// Fusion of test29 and test36. 

// Putter1:            Putter2:           Getter1:       Getter2:
//    MU1.Lock()          MU1.Lock()                                    
//    write(GLOB)         write(GLOB)                                   
//    MU1.Unlock()        MU1.Unlock()                                  
//    Q1.Put()            Q2.Put()                                      
//    Q1.Put()            Q2.Put()                                      
//    MU1.Lock()          MU1.Lock()        
//    MU2.Lock()          MU2.Lock()        
//    write(GLOB)         write(GLOB)       
//    MU2.Unlock()        MU2.Unlock()      
//    MU1.Unlock()        MU1.Unlock()     sleep          sleep
//                                         Q1.Get()       Q1.Get()
//                                         Q2.Get()       Q2.Get()
//                                         MU2.Lock()     MU2.Lock()
//                                         write(GLOB)    write(GLOB)
//                                         MU2.Unlock()   MU2.Unlock()
//


ProducerConsumerQueue *Q1, *Q2;
int     GLOB = 0;

void Putter(ProducerConsumerQueue *q) {
  MU1.Lock();
  GLOB++;
  MU1.Unlock();

  q->Put(NULL);
  q->Put(NULL);

  MU1.Lock();
  MU2.Lock();
  GLOB++;
  MU2.Unlock();
  MU1.Unlock();

}

void Putter1() { Putter(Q1); }
void Putter2() { Putter(Q2); }

void Getter() {
  usleep(100000);
  Q1->Get();
  Q2->Get();

  MU2.Lock();
  GLOB++;
  MU2.Unlock();

  usleep(48000); //  TODO: remove this when FP in test32 is fixed. 
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test38. FP.");
  printf("test38: negative\n");
  Q1 = new ProducerConsumerQueue(INT_MAX);
  Q2 = new ProducerConsumerQueue(INT_MAX);
  MyThreadArray t(Getter, Getter, Putter1, Putter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
  delete Q1;
  delete Q2;
}
REGISTER_TEST(Run, 38);
}  // namespace test38

// test39: FP. Barrier. {{{1
namespace test39 {
#ifndef NO_BARRIER
// Same as test17 but uses Barrier class (pthread_barrier_t). 
int     GLOB = 0;
const int N_threads = 3;
Barrier barrier(N_threads);

void Worker() {
  MU.Lock();
  GLOB++;
  MU.Unlock();
  barrier.Block();
  CHECK(GLOB == N_threads);
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test39. FP. Fixed by MSMProp1. Barrier.");
  printf("test39: negative\n");
  {
    ThreadPool pool(N_threads);
    pool.StartWorkers();
    for (int i = 0; i < N_threads; i++) {
      pool.Add(NewCallback(Worker));
    }
  } // all folks are joined here. 
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 39);
#endif // NO_BARRIER
}  // namespace test39


// test40: FP. Synchronization via Mutexes and PCQ. 4 threads. W/W {{{1
namespace test40 {
// Similar to test38 but with different order of events (due to sleep). 

// Putter1:            Putter2:           Getter1:       Getter2:
//    MU1.Lock()          MU1.Lock()                                    
//    write(GLOB)         write(GLOB)                                   
//    MU1.Unlock()        MU1.Unlock()                                  
//    Q1.Put()            Q2.Put()                                      
//    Q1.Put()            Q2.Put()                                      
//                                        Q1.Get()       Q1.Get()     
//                                        Q2.Get()       Q2.Get()     
//                                        MU2.Lock()     MU2.Lock()   
//                                        write(GLOB)    write(GLOB)  
//                                        MU2.Unlock()   MU2.Unlock()
//                                         
//    MU1.Lock()          MU1.Lock()                                       
//    MU2.Lock()          MU2.Lock()                                       
//    write(GLOB)         write(GLOB)                                      
//    MU2.Unlock()        MU2.Unlock()                                     
//    MU1.Unlock()        MU1.Unlock() 


ProducerConsumerQueue *Q1, *Q2;
int     GLOB = 0;

void Putter(ProducerConsumerQueue *q) {
  MU1.Lock();
  GLOB++;
  MU1.Unlock();

  q->Put(NULL);
  q->Put(NULL);
  usleep(100000);

  MU1.Lock();
  MU2.Lock();
  GLOB++;
  MU2.Unlock();
  MU1.Unlock();

}

void Putter1() { Putter(Q1); }
void Putter2() { Putter(Q2); }

void Getter() {
  Q1->Get();
  Q2->Get();

  MU2.Lock();
  GLOB++;
  MU2.Unlock();

  usleep(48000); //  TODO: remove this when FP in test32 is fixed. 
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test40. FP. Fixed by MSMProp1. Complex Stuff.");
  printf("test40: negative\n");
  Q1 = new ProducerConsumerQueue(INT_MAX);
  Q2 = new ProducerConsumerQueue(INT_MAX);
  MyThreadArray t(Getter, Getter, Putter1, Putter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
  delete Q1;
  delete Q2;
}
REGISTER_TEST(Run, 40);
}  // namespace test40

// test41: TN. Test for race that appears when loading a dynamic symbol. {{{1
namespace test41 {
void Worker() {
  ANNOTATE_NO_OP(NULL); // An empty function, loaded from dll. 
}
void Run() {
  printf("test41: negative\n");
  MyThreadArray t(Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 41, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test41


// test42: TN. Using the same cond var several times. {{{1
namespace test42 {
int GLOB = 0;
int COND = 0;
int N_threads = 3;

void Worker1() {
  GLOB=1;

  MU.Lock(); 
  COND = 1;
  CV.Signal();
  MU.Unlock();

  MU.Lock(); 
  while (COND != 0) 
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);
  MU.Unlock();

  GLOB=3;

}

void Worker2() {

  MU.Lock(); 
  while (COND != 1) 
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);
  MU.Unlock();

  GLOB=2;

  MU.Lock(); 
  COND = 0;
  CV.Signal();
  MU.Unlock();

}

void Run() {
//  ANNOTATE_EXPECT_RACE(&GLOB, "test42. TN. debugging.");
  printf("test42: negative\n");
  MyThreadArray t(Worker1, Worker2);
  t.Start(); 
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 42, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test42



// test43: TN. {{{1
namespace test43 {
// 
// Putter:            Getter: 
// 1. write          
// 2. Q.Put() --\     .
// 3. read       \--> a. Q.Get()    
//                    b. read
int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX);
void Putter() {
  GLOB = 1;
  Q.Put(NULL);
  CHECK(GLOB == 1);
}
void Getter() {
  Q.Get();
  usleep(100000);
  CHECK(GLOB == 1);
}
void Run() {
  printf("test43: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 43)
}  // namespace test43


// test44: FP. {{{1
namespace test44 {
// 
// Putter:            Getter: 
// 1. read          
// 2. Q.Put() --\     .
// 3. MU.Lock()  \--> a. Q.Get()    
// 4. write
// 5. MU.Unlock()                   
//                    b. MU.Lock()
//                    c. write
//                    d. MU.Unlock();              
int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX);
void Putter() {
  CHECK(GLOB == 0);
  Q.Put(NULL);
  MU.Lock();
  GLOB = 1;
  MU.Unlock();
}
void Getter() {
  Q.Get();
  usleep(100000);
  MU.Lock();
  GLOB = 1;
  MU.Unlock();
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test44. FP. Fixed by MSMProp1.");
  printf("test44: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 44)
}  // namespace test44


// test45: TN. {{{1
namespace test45 {
// 
// Putter:            Getter: 
// 1. read          
// 2. Q.Put() --\     .
// 3. MU.Lock()  \--> a. Q.Get()    
// 4. write
// 5. MU.Unlock()                   
//                    b. MU.Lock()
//                    c. read
//                    d. MU.Unlock();              
int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX);
void Putter() {
  CHECK(GLOB == 0);
  Q.Put(NULL);
  MU.Lock();
  GLOB++;
  MU.Unlock();
}
void Getter() {
  Q.Get();
  usleep(100000);
  MU.Lock();
  CHECK(GLOB <= 1);
  MU.Unlock();
}
void Run() {
  printf("test45: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 45)
}  // namespace test45


// test46: FN. {{{1
namespace test46 {
// 
// First:                             Second: 
// 1. write                          
// 2. MU.Lock()                      
// 3. write                       
// 4. MU.Unlock()                      (sleep)              
//                                    a. MU.Lock()
//                                    b. write
//                                    c. MU.Unlock();              
int     GLOB = 0;
void First() {
  GLOB++;
  MU.Lock();
  GLOB++;
  MU.Unlock();
}
void Second() {
  usleep(480000);
  MU.Lock();
  GLOB++;
  MU.Unlock();

  // just a print. 
  // If we move it to Run()  we will get report in MSMHelgrind 
  // due to its false positive (test32). 
  MU.Lock();
  printf("\tGLOB=%d\n", GLOB);
  MU.Unlock();
}
void Run() {
  printf("test46: (false) negative\n");
  MyThreadArray t(First, Second);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 46)
}  // namespace test46


// test47: TP. Not detected by pure happens-before detectors. {{{1
namespace test47 {
// A true race that can not be detected by a pure happens-before 
// race detector. 
//
// First:                             Second: 
// 1. write                          
// 2. MU.Lock()                      
// 3. MU.Unlock()                      (sleep)              
//                                    a. MU.Lock()
//                                    b. MU.Unlock();              
//                                    c. write
int     GLOB = 0;
void First() {
  GLOB=1;
  MU.Lock();
  MU.Unlock();
}
void Second() {
  usleep(480000);
  MU.Lock();
  MU.Unlock();
  GLOB++;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test47. TP. Not detected by pure HB.");
  printf("test47: positive\n");
  MyThreadArray t(First, Second);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 47)
}  // namespace test47


// test48: FN. Simple race (single write vs multiple reads). {{{1
namespace test48 {
int     GLOB = 0;
// same as test10 but with single writer and  multiple readers
// A simple data race between single writer and  multiple readers. 
// Write happens before Reads (enforced by sleep(1)), 

// 
// Writer:                    Readers:
// 1. write(GLOB)             a. sleep(long enough so that GLOB 
//                                is most likely initialized by Writer)
//                            b. read(GLOB)
// 
//
// Eraser algorithm does not detect the race here, 
// see Section 2.2 of http://citeseer.ist.psu.edu/savage97eraser.html. 
//
void Writer() {
  GLOB = 3; 
}
void Reader() {
  usleep(100000);
  CHECK(GLOB != -777);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test48. FN in MSMHelgrind.");
  printf("test48: positive\n");
  MyThreadArray t(Writer, Reader,Reader,Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 48)
}  // namespace test48


// test49: FN. Simple race (single write vs multiple reads). {{{1
namespace test49 {
int     GLOB = 0;
// same as test10 but with multiple read operations done by a single reader
// A simple data race between writer and readers. 
// Write happens before Read (enforced by sleep(1)), 
// 
// Writer:                    Reader:
// 1. write(GLOB)             a. sleep(long enough so that GLOB 
//                                is most likely initialized by Writer)
//                            b. read(GLOB)
//                            c. read(GLOB)
//                            d. read(GLOB)
//                            e. read(GLOB)
// 
//
// Eraser algorithm does not detect the race here, 
// see Section 2.2 of http://citeseer.ist.psu.edu/savage97eraser.html. 
//
void Writer() {
  GLOB = 3; 
}
void Reader() {
  usleep(100000);
  CHECK(GLOB != -777);
  CHECK(GLOB != -777);
  CHECK(GLOB != -777);
  CHECK(GLOB != -777);
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test49. FN in MSMHelgrind.");
  printf("test49: positive\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 49);
}  // namespace test49


// test50: TP. Synchronization via CondVar. {{{1
namespace test50 {
int     GLOB = 0;
// Two last write accesses to GLOB are not synchronized 
//
// Waiter:                      Waker: 
// 1. COND = 0
// 2. Start(Waker)              
// 3. MU.Lock()                 a. write(GLOB)
//                              b. MU.Lock()
//                              c. COND = 1
//                         /--- d. CV.Signal()
//  4. while(COND)        /     e. MU.Unock()
//       CV.Wait(MU) <---/
//  5. MU.Unlock()
//  6. write(GLOB)              f. MU.Lock()
//                              g. write(GLOB)
//                              h. MU.Unlock()


void Waker() {
  usleep(10000);  // Make sure the waiter blocks.

  GLOB = 1;

  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();

  MU.Lock();
  GLOB = 3; 
  MU.Unlock();


}

void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  COND = 0;
  pool.Add(NewCallback(Waker));
 
  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);
  MU.Unlock();

  GLOB = 2;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test50. TP.");
  printf("test50: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 50, FEATURE|NEEDS_ANNOTATIONS);
}  // namespace test50


// test51: TP. Synchronization via CondVar: problem with several signals. {{{1
namespace test51 {
int     GLOB = 0;
int     COND = 0;


// scheduler dependent results because of several signals
// second signal will be lost
//
// Waiter:                      Waker: 
// 1. Start(Waker)              
// 2. MU.Lock()          
// 3. while(COND)               
//       CV.Wait(MU)<-\         .
// 4. MU.Unlock()      \        .
// 5. write(GLOB)       \       a. write(GLOB)
//                       \      b. MU.Lock()
//                        \     c. COND = 1
//                         \--- d. CV.Signal()
//                              e. MU.Unock()
//                              
//                              f. write(GLOB)
//                              
//                              g. MU.Lock()
//                              h. COND = 1
//                    LOST<---- i. CV.Signal()
//                              j. MU.Unlock()

void Waker() {

  usleep(10000);  // Make sure the waiter blocks.

  GLOB = 1;
  
  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();

  usleep(10000);  // Make sure the waiter is signalled.

  GLOB = 2;

  MU.Lock();
  COND = 1;
  CV.Signal();   //Lost Signal
  MU.Unlock();
}

void Waiter() {

  ThreadPool pool(1);
  pool.StartWorkers();
  pool.Add(NewCallback(Waker));
 
  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  MU.Unlock();


  GLOB = 3;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test51. TP.");
  printf("test51: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 51);
}  // namespace test51


// test52: TP. Synchronization via CondVar: problem with several signals. {{{1
namespace test52 {
int     GLOB = 0;
int     COND = 0;

// same as test51 but the first signal will be lost
// scheduler dependent results because of several signals
//
// Waiter:                      Waker: 
// 1. Start(Waker)              
//                              a. write(GLOB)
//                              b. MU.Lock()
//                              c. COND = 1
//                    LOST<---- d. CV.Signal()
//                              e. MU.Unock()
//                              
// 2. MU.Lock()       
// 3. while(COND)               
//       CV.Wait(MU)<-\         .
// 4. MU.Unlock()      \        f. write(GLOB)
// 5. write(GLOB)       \       .
//                       \      g. MU.Lock()
//                        \     h. COND = 1
//                         \--- i. CV.Signal()
//                              j. MU.Unlock()

void Waker() {

  GLOB = 1;
  
  MU.Lock();
  COND = 1;
  CV.Signal();    //lost signal
  MU.Unlock();

  usleep(20000);  // Make sure the waiter blocks

  GLOB = 2;

  MU.Lock();
  COND = 1;
  CV.Signal(); 
  MU.Unlock();  
}

void Waiter() {
  ThreadPool pool(1);
  pool.StartWorkers();
  pool.Add(NewCallback(Waker));
 
  usleep(10000);  // Make sure the first signal will be lost

  MU.Lock();
  while(COND != 1)
    CV.Wait(&MU);
  MU.Unlock();
  
  GLOB = 3;
}
void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test52. TP.");
  printf("test52: positive\n");
  Waiter();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 52);
}  // namespace test52


// test53: TN. Synchronization via implicit semaphore. {{{1
namespace test53 {
// Correctly synchronized test, but the common lockset is empty.
// The variable FLAG works as an implicit semaphore. 
// MSMHelgrind still does not complain since it does not maintain the lockset
// at the exclusive state. But MSMProp1 does complain. 
// See also test54. 
// 
//
// Initializer:                  Users
// 1. MU1.Lock() 
// 2. write(GLOB) 
// 3. FLAG = true
// 4. MU1.Unlock()
//                               a. MU1.Lock()
//                               b. f = FLAG;
//                               c. MU1.Unlock()
//                               d. if (!f) goto a.
//                               e. MU2.Lock()
//                               f. write(GLOB)
//                               g. MU2.Unlock()
//

int     GLOB = 0;
bool    FLAG = false;

void Initializer() {
  MU1.Lock();
  GLOB = 1000;
  FLAG = true;
  MU1.Unlock();
  usleep(100000); // just in case
}

void User() {
  bool f = false;
  while(!f) {
    MU1.Lock();
    f = FLAG;
    MU1.Unlock();
    usleep(10000);
  }
  // at this point Initializer will not access GLOB again
  MU2.Lock();
  CHECK(GLOB >= 1000);
  GLOB++;
  MU2.Unlock();
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test53. Implicit semaphore");
  printf("test53: positive\n");
  MyThreadArray t(Initializer, User, User);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 53)
}  // namespace test53


// test54: TN. Synchronization via implicit semaphore. Annotated {{{1
namespace test54 {
// Same as test53, but annotated. 
int     GLOB = 0;
bool    FLAG = false;

void Initializer() {
  MU1.Lock();
  GLOB = 1000;
  FLAG = true;
  ANNOTATE_CONDVAR_SIGNAL(&GLOB);
  MU1.Unlock();
  usleep(100000); // just in case
}

void User() {
  bool f = false;
  while(!f) {
    MU1.Lock();
    f = FLAG;
    MU1.Unlock();
    usleep(10000);
  }
  // at this point Initializer will not access GLOB again
  ANNOTATE_CONDVAR_WAIT(&GLOB);
  MU2.Lock();
  CHECK(GLOB >= 1000);
  GLOB++;
  MU2.Unlock();
}

void Run() {
  printf("test54: negative\n");
  MyThreadArray t(Initializer, User, User);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 54, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test54


// test55: FP. Synchronization with TryLock. Not easy for race detectors {{{1
namespace test55 {  
// "Correct" synchronization with TryLock and Lock. 
//
// This scheme is actually very risky. 
// It is covered in detail in this video: 
// http://youtube.com/watch?v=mrvAqvtWYb4 (slide 36, near 50-th minute). 
int     GLOB = 0;

void Worker_Lock() {
  GLOB = 1;
  MU.Lock();
}

void Worker_TryLock() {
  while (true) {
    if (!MU.TryLock()) {
      MU.Unlock();
      break;
    }
    else 
      MU.Unlock();
    usleep(100); 
  }
  GLOB = 2; 
}

void Run() {
  printf("test55:\n");
  MyThreadArray t(Worker_Lock, Worker_TryLock);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 55, FEATURE|EXCLUDE_FROM_ALL);
}  // namespace test55



// test56: TP. Use of ANNOTATE_BENIGN_RACE. {{{1
namespace test56 {
// For whatever reason the user wants to treat 
// a race on GLOB as a benign race. 
int     GLOB = 0;
int     GLOB2 = 0;

void Worker() {
  GLOB++;
}

void Run() {
  ANNOTATE_BENIGN_RACE(&GLOB, "test56. Use of ANNOTATE_BENIGN_RACE.");
  ANNOTATE_BENIGN_RACE(&GLOB2, "No race. The tool should be silent");
  printf("test56: positive\n");
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 56, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test56


// test57: TN: Correct use of atomics. {{{1
namespace test57 {
int     GLOB = 0;
void Writer() {
  for (int i = 0; i < 10; i++) {
    __sync_add_and_fetch(&GLOB, 1);
    usleep(1000);
  }
}
void Reader() {
  while (GLOB < 20) usleep(1000);
}
void Run() {
  printf("test57: negative\n");
  MyThreadArray t(Writer, Writer, Reader, Reader);
  t.Start();
  t.Join();
  CHECK(GLOB == 20);
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 57)
}  // namespace test57


// test58: TN. User defined synchronization. {{{1
namespace test58 {
int     GLOB1 = 1;
int     GLOB2 = 2;
int     FLAG1 = 0;
int     FLAG2 = 0;

// Correctly synchronized test, but the common lockset is empty.
// The variables FLAG1 and FLAG2 used for synchronization and as 
// temporary variables for swapping two global values.
// Such kind of synchronization is rarely used (Excluded from all tests??).

void Worker2() {
  FLAG1=GLOB2;

  while(!FLAG2);
  GLOB2=FLAG2;
}

void Worker1() {
  FLAG2=GLOB1;

  while(!FLAG1);
  GLOB1=FLAG1;
}

void Run() {
  printf("test58:\n");
  MyThreadArray t(Worker1, Worker2);
  t.Start();
  t.Join();
  printf("\tGLOB1=%d\n", GLOB1);
  printf("\tGLOB2=%d\n", GLOB2);
}
REGISTER_TEST2(Run, 58, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test58



// test59: TN. User defined synchronization. Annotated {{{1
namespace test59 {
int     COND1 = 0;
int     COND2 = 0;
int     GLOB1 = 1;
int     GLOB2 = 2;
int     FLAG1 = 0;
int     FLAG2 = 0;
// same as test 58 but annotated

void Worker2() {
  FLAG1=GLOB2;
  ANNOTATE_CONDVAR_SIGNAL(&COND2);
  while(!FLAG2);
  ANNOTATE_CONDVAR_WAIT(&COND1);
  GLOB2=FLAG2;
}

void Worker1() {
  FLAG2=GLOB1;
  ANNOTATE_CONDVAR_SIGNAL(&COND1);
  while(!FLAG1);
  ANNOTATE_CONDVAR_WAIT(&COND2);
  GLOB1=FLAG1;
}

void Run() {
  printf("test59: negative\n");
  ANNOTATE_BENIGN_RACE(&FLAG1, "synchronization via 'safe' race");
  ANNOTATE_BENIGN_RACE(&FLAG2, "synchronization via 'safe' race");
  MyThreadArray t(Worker1, Worker2);
  t.Start();
  t.Join();
  printf("\tGLOB1=%d\n", GLOB1);
  printf("\tGLOB2=%d\n", GLOB2);
}
REGISTER_TEST2(Run, 59, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test59


// test60: TN. Correct synchronization using signal-wait {{{1
namespace test60 {
int     COND1 = 0;
int     COND2 = 0;
int     GLOB1 = 1;
int     GLOB2 = 2;
int     FLAG2 = 0;
int     FLAG1 = 0;
// same as test 59 but synchronized with signal-wait.

void Worker2() {
  FLAG1=GLOB2;

  MU.Lock();
  COND1 = 1;
  CV.Signal();    
  MU.Unlock();

  MU.Lock();
  while(COND2 != 1)
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);
  MU.Unlock();

  GLOB2=FLAG2;
}

void Worker1() {
  FLAG2=GLOB1;

  MU.Lock();
  COND2 = 1;
  CV.Signal();    
  MU.Unlock();

  MU.Lock();
  while(COND1 != 1)
    CV.Wait(&MU);
  ANNOTATE_CONDVAR_LOCK_WAIT(&CV, &MU);
  MU.Unlock();

  GLOB1=FLAG1;
}

void Run() {
  printf("test60: negative\n");
  MyThreadArray t(Worker1, Worker2);
  t.Start();
  t.Join();
  printf("\tGLOB1=%d\n", GLOB1);
  printf("\tGLOB2=%d\n", GLOB2);
}
REGISTER_TEST2(Run, 60, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test60


// test61: TN. Synchronization via Mutex as in happens-before, annotated. {{{1
namespace test61 {
Mutex MU;
int     GLOB = 0;
int     *P1 = NULL, *P2 = NULL;

// In this test Mutex lock/unlock operations introduce happens-before relation. 
// We annotate the code so that MU is treated as in pure happens-before detector. 


void Putter() {
  ANNOTATE_MUTEX_IS_USED_AS_CONDVAR(&MU);
  MU.Lock();
  if (P1 == NULL) {
    P1 = &GLOB;
    *P1 = 1;
  } 
  MU.Unlock();
}

void Getter() {
  bool done  = false;
  while (!done) {
    MU.Lock();
    if (P1) {
      done = true;
      P2 = P1; 
      P1 = NULL;
    }
    MU.Unlock();
  }
  *P2 = 2;
}


void Run() {
  printf("test61: negative\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 61, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test61


// test62: STAB. Create as many segments as possible. {{{1
namespace test62 {
// Helgrind 3.3.0 will fail as it has a hard limit of < 2^24 segments. 
// A better scheme is to implement garbage collection for segments. 
ProducerConsumerQueue Q(INT_MAX);
const int N = 1 << 22;

void Putter() {
  for (int i = 0; i < N; i++){
    if ((i % (N / 8)) == 0) {
      printf("i=%d\n", i);
    }
    Q.Put(NULL);
  }
}

void Getter() {
  for (int i = 0; i < N; i++)
    Q.Get();
}

void Run() {
  printf("test62:\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 62, STABILITY|EXCLUDE_FROM_ALL)
}  // namespace test62


// test63: STAB. Create as many segments as possible and do it fast. {{{1
namespace test63 {
// Helgrind 3.3.0 will fail as it has a hard limit of < 2^24 segments. 
// A better scheme is to implement garbage collection for segments. 
const int N = 1 << 24;
int C = 0;

void Putter() {
  for (int i = 0; i < N; i++){
    if ((i % (N / 8)) == 0) {
      printf("i=%d\n", i);
    }
    ANNOTATE_CONDVAR_SIGNAL(&C);
  }
}

void Getter() {
}

void Run() {
  printf("test63:\n");
  MyThreadArray t(Putter, Getter);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 63, STABILITY|EXCLUDE_FROM_ALL)
}  // namespace test63


// test64: TP. T2 happens-before T3, but T1 is independent. Reads in T1/T2. {{{1
namespace test64 {
// True race between T1 and T3: 
//
// T1:                   T2:                   T3: 
// 1. read(GLOB)         (sleep)
//                       a. read(GLOB)
//                       b. Q.Put() ----->    A. Q.Get()
//                                            B. write(GLOB) 
//
//

int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX);

void T1() {
  CHECK(GLOB == 0);
}

void T2() {
  usleep(100000);
  CHECK(GLOB == 0);
  Q.Put(NULL);
}

void T3() {
  Q.Get();
  GLOB = 1;
}


void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "TP.");
  printf("test64: positive\n");
  MyThreadArray t(T1, T2, T3);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 64)
}  // namespace test64


// test65: TP. T2 happens-before T3, but T1 is independent. Writes in T1/T2. {{{1
namespace test65 {
// Similar to test64. 
// True race between T1 and T3: 
//
// T1:                   T2:                   T3: 
// 1. MU.Lock()
// 2. write(GLOB)
// 3. MU.Unlock()         (sleep)
//                       a. MU.Lock()
//                       b. write(GLOB)
//                       c. MU.Unlock()
//                       d. Q.Put() ----->    A. Q.Get()
//                                            B. write(GLOB) 
//
//

int     GLOB = 0;
ProducerConsumerQueue Q(INT_MAX);

void T1() {
  MU.Lock();
  GLOB++;
  MU.Unlock();
}

void T2() {
  usleep(100000);
  MU.Lock();
  GLOB++;
  MU.Unlock();
  Q.Put(NULL);
}

void T3() {
  Q.Get();
  GLOB = 1;
}


void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "TP.");
  printf("test65: positive\n");
  MyThreadArray t(T1, T2, T3);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 65)
}  // namespace test65


// test66: TN. Two separate pairs of signaller/waiter using the same CV. {{{1
namespace test66 {
int     GLOB1 = 0;
int     GLOB2 = 0;
int     C1 = 0;
int     C2 = 0;

void Signaller1() {
  GLOB1 = 1;
  MU.Lock();
  C1 = 1;
  CV.Signal();
  MU.Unlock();
}

void Signaller2() {
  GLOB2 = 1;
  usleep(100000);
  MU.Lock();
  C2 = 1;
  CV.Signal();
  MU.Unlock();
}

void Waiter1() {
  MU.Lock();
  while (C1 != 1) CV.Wait(&MU);
  ANNOTATE_CONDVAR_WAIT(&CV);
  MU.Unlock();
  GLOB1 = 2;
}

void Waiter2() {
  MU.Lock();
  while (C2 != 1) CV.Wait(&MU);
  ANNOTATE_CONDVAR_WAIT(&CV);
  MU.Unlock();
  GLOB2 = 2;
}

void Run() {
  printf("test66: negative\n");
  MyThreadArray t(Signaller1, Signaller2, Waiter1, Waiter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d/%d\n", GLOB1, GLOB2);
}
REGISTER_TEST2(Run, 66, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test66


// test67: FN. Race between Signaller1 and Waiter2 {{{1
namespace test67 {
// Similar to test66, but there is a real race here. 
//
// Here we create a happens-before arc between Signaller1 and Waiter2
// even though there should be no such arc. 
// However, it's probably improssible (or just very hard) to avoid it. 
int     GLOB = 0;
int     C1 = 0;
int     C2 = 0;

void Signaller1() {
  GLOB = 1;
  MU.Lock();
  C1 = 1;
  CV.Signal();
  MU.Unlock();
}

void Signaller2() {
  usleep(100000);
  MU.Lock();
  C2 = 1;
  CV.Signal();
  MU.Unlock();
}

void Waiter1() {
  MU.Lock();
  while (C1 != 1) CV.Wait(&MU);
  ANNOTATE_CONDVAR_WAIT(&CV);
  MU.Unlock();
}

void Waiter2() {
  MU.Lock();
  while (C2 != 1) CV.Wait(&MU);
  ANNOTATE_CONDVAR_WAIT(&CV);
  MU.Unlock();
  GLOB = 2;
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "test67. FN. Race between Signaller1 and Waiter2");
  printf("test67: positive\n");
  MyThreadArray t(Signaller1, Signaller2, Waiter1, Waiter2);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 67, FEATURE|NEEDS_ANNOTATIONS)
}  // namespace test67


// test68: TP. Writes are protected by MU, reads are not. {{{1
namespace test68 {
// In this test, all writes to GLOB are protected by a mutex 
// but some reads go unprotected. 
// This is certainly a race, but in some cases such code could occur in 
// a correct program. For example, the unprotected reads may be used 
// for showing statistics and are not required to be precise. 
int     GLOB = 0;
int     COND = 0;
const int N_writers = 3;

void Writer() {
  for (int i = 0; i < 100; i++) {
    MU.Lock();
    GLOB++;
    MU.Unlock();
  }

  // we are done
  MU1.Lock();
  COND++;
  MU1.Unlock();
}

void Reader() {
  bool cont = true;
  while (cont) {
    CHECK(GLOB >= 0);

    // are we done?
    MU1.Lock();
    if (COND == N_writers)
      cont = false;
    MU1.Unlock();
  }
}

void Run() {
  ANNOTATE_EXPECT_RACE(&GLOB, "TP. Writes are protected, reads are not.");
  printf("test68: positive\n");
  MyThreadArray t(Reader, Writer, Writer, Writer);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 68)
}  // namespace test68


// test69:  {{{1
namespace test69 {
// This is the same as test68, but annotated. 
// We do not want to annotate GLOB as a benign race 
// because we want to allow racy reads only in certain places. 
//
// TODO: 
int     GLOB = 0;
int     COND = 0;
const int N_writers = 3;
int     FAKE_MU = 0;

void Writer() {
  for (int i = 0; i < 10; i++) {
    MU.Lock();
    GLOB++;
    MU.Unlock();
  }

  // we are done
  MU1.Lock();
  COND++;
  MU1.Unlock();
}

void Reader() {
  bool cont = true;
  while (cont) {
    ANNOTATE_IGNORE_READS_BEGIN();
    CHECK(GLOB >= 0);
    ANNOTATE_IGNORE_READS_END();

    // are we done?
    MU1.Lock();
    if (COND == N_writers)
      cont = false;
    MU1.Unlock();
  }
}

void Run() {
  printf("test69: negative\n");
  MyThreadArray t(Reader, Writer, Writer, Writer);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 69)
}  // namespace test69

// test70: STAB. Check that TRACE_MEMORY works. {{{1
namespace test70 {
int     GLOB = 0;
void Run() {
  printf("test70: negative\n");
  ANNOTATE_TRACE_MEMORY(&GLOB);
  GLOB = 1;
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 70)
}  // namespace test70



// test71: TN. strlen, index. {{{1
namespace test71 {
// This test is a reproducer for a benign race in strlen (as well as index, etc). 
// Some implementations of strlen may read up to 7 bytes past the end of the string 
// thus touching memory which may not belong to this string. 
// Such race is benign because the data read past the end of the string is not used.
//
// Here, we allocate a 8-byte aligned string str and initialize first 5 bytes.
// Then one thread calls strlen(str) (as well as index & rindex)
// and another thread initializes str[5]..str[7]. 
//
// This can be fixed in Helgrind by intercepting strlen and replacing it 
// with a simpler implementation. 

char    *str;
void WorkerX() {
  usleep(100000);
  CHECK(strlen(str) == 4);
  CHECK(index(str, 'X') == str);
  CHECK(index(str, 'x') == str+1);
  CHECK(index(str, 'Y') == NULL);
  CHECK(rindex(str, 'X') == str+2);
  CHECK(rindex(str, 'x') == str+3);
  CHECK(rindex(str, 'Y') == NULL);
}
void WorkerY() {
  str[5] = 'Y';
  str[6] = 'Y';
  str[7] = '\0';
}

void Run() {
  str = new char[8];
  str[0] = 'X';
  str[1] = 'x';
  str[2] = 'X';
  str[3] = 'x';
  str[4] = '\0';

  printf("test71: negative (strlen & index)\n");
  MyThread t1(WorkerY);
  MyThread t2(WorkerX);
  t1.Start();
  t2.Start();
  t1.Join();
  t2.Join();
  printf("\tstrX=%s; strY=%s\n", str, str+5);
}
REGISTER_TEST(Run, 71)
}  // namespace test71


// test72: STAB. Stress test for the number of segment sets (SSETs). {{{1
namespace test72 {
// Variation of test33. 
// Instead of creating Nlog*N_iter threads, 
// we create Nlog threads and do N_iter barriers. 
int     GLOB = 0;
const int N_iter = 30;
const int Nlog  = 16;
const int N     = 1 << Nlog;
static int64_t ARR1[N];
static int64_t ARR2[N];
Barrier *barriers[N_iter];

void Worker() {
  MU.Lock();
  int n = ++GLOB;
  MU.Unlock();

  n %= Nlog;

  for (int it = 0; it < N_iter; it++) {
    // Iterate N_iter times, block on barrier after each iteration. 
    // This way Helgrind will create new segments after each barrier. 

    for (int x = 0; x < 2; x++) { 
      // run the inner loop twice. 
      // When a memory location is accessed second time it is likely 
      // that the state (SVal) will be unchanged. 
      // The memory machine may optimize this case. 
      for (int i = 0; i < N; i++) {
        // ARR1[i] and ARR2[N-1-i] are accessed by threads from i-th subset 
        if (i & (1 << n)) {
          CHECK(ARR1[i] == 0);
          CHECK(ARR2[N-1-i] == 0);
        }
      }
    }
    barriers[it]->Block();
  }
}


void Run() {
  printf("test72:\n");

  std::vector<MyThread*> vec(Nlog);

  for (int i = 0; i < N_iter; i++)
    barriers[i] = new Barrier(Nlog);

  // Create and start Nlog threads
  for (int i = 0; i < Nlog; i++) {
    vec[i] = new MyThread(Worker);
    vec[i]->Start();
  }
  
  // Join all threads. 
  for (int i = 0; i < Nlog; i++) {
    vec[i]->Join();
    delete vec[i];
  }
  for (int i = 0; i < N_iter; i++)
    delete barriers[i];

  printf("\tGLOB=%d; ARR[1]=%d; ARR[7]=%d; ARR[N-1]=%d\n", 
         GLOB, (int)ARR1[1], (int)ARR1[7], (int)ARR1[N-1]);
}
REGISTER_TEST2(Run, 72, STABILITY|PERFORMANCE|EXCLUDE_FROM_ALL);
}  // namespace test72


// test73: STAB. Stress test for the number of (SSETs), different access sizes. {{{1
namespace test73 {
// Variation of test72. 
// We perform accesses of different sizes to the same location. 
int     GLOB = 0;
const int N_iter = 10;
const int Nlog  = 16;
const int N     = 1 << Nlog;
static int64_t ARR1[N];
static int ARR2[N];
Barrier *barriers[N_iter];

void Worker() {
  MU.Lock();
  int n = ++GLOB;
  MU.Unlock();

  n %= Nlog;

  for (int it = 0; it < N_iter; it++) {
    // Iterate N_iter times, block on barrier after each iteration. 
    // This way Helgrind will create new segments after each barrier. 

    for (int x = 0; x < 4; x++) { 
      for (int i = 0; i < N; i++) {
        // ARR1[i] are accessed by threads from i-th subset 
        if (i & (1 << n)) {
          for (int off = 0; off < (1 << x); off++) {
            switch(x) {
              case 0: CHECK(          ARR1  [i * (1<<x) + off] == 0); break;
              case 1: CHECK(((int*)  (ARR1))[i * (1<<x) + off] == 0); break;
              case 2: CHECK(((short*)(ARR1))[i * (1<<x) + off] == 0); break;
              case 3: CHECK(((char*) (ARR1))[i * (1<<x) + off] == 0); break;
            }
            switch(x) {
              case 1: CHECK(((int*)  (ARR2))[i * (1<<x) + off] == 0); break;
              case 2: CHECK(((short*)(ARR2))[i * (1<<x) + off] == 0); break;
              case 3: CHECK(((char*) (ARR2))[i * (1<<x) + off] == 0); break;
            }
          }
        }
      }
    }
    barriers[it]->Block();
  }
}



void Run() {
  printf("test73:\n");

  std::vector<MyThread*> vec(Nlog);

  for (int i = 0; i < N_iter; i++)
    barriers[i] = new Barrier(Nlog);

  // Create and start Nlog threads
  for (int i = 0; i < Nlog; i++) {
    vec[i] = new MyThread(Worker);
    vec[i]->Start();
  }
  
  // Join all threads. 
  for (int i = 0; i < Nlog; i++) {
    vec[i]->Join();
    delete vec[i];
  }
  for (int i = 0; i < N_iter; i++)
    delete barriers[i];

  printf("\tGLOB=%d; ARR[1]=%d; ARR[7]=%d; ARR[N-1]=%d\n", 
         GLOB, (int)ARR1[1], (int)ARR1[7], (int)ARR1[N-1]);
}
REGISTER_TEST2(Run, 73, STABILITY|PERFORMANCE|EXCLUDE_FROM_ALL);
}  // namespace test73


// test74: PERF. A lot of lock/unlock calls. {{{1
namespace    test74 {
const int N = 100000;
void Run() {
  printf("test74: perf\n");
  for (int i = 0; i < N; i++ ) {
    MU.Lock();
    MU.Unlock();
  }
}
REGISTER_TEST(Run, 74)
}  // namespace test74


// test75: TN. Test for sem_post, sem_wait, sem_trywait. {{{1
namespace test75 {
int     GLOB = 0;
sem_t   sem[2];

void Poster() {
  GLOB = 1;
  sem_post(&sem[0]);
  sem_post(&sem[1]);
}

void Waiter() {
  sem_wait(&sem[0]);
  CHECK(GLOB==1);
}
void TryWaiter() {
  usleep(500000);
  sem_trywait(&sem[1]);
  CHECK(GLOB==1);
}

void Run() {
  sem_init(&sem[0], 0, 0);
  sem_init(&sem[1], 0, 0);

  printf("test75: negative\n");
  {
    MyThreadArray t(Poster, Waiter);
    t.Start();
    t.Join();
  }
  GLOB = 2;
  {
    MyThreadArray t(Poster, TryWaiter);
    t.Start();
    t.Join();
  }
  printf("\tGLOB=%d\n", GLOB);

  sem_destroy(&sem[0]);
  sem_destroy(&sem[1]);
}
REGISTER_TEST(Run, 75)
}  // namespace test75

// RefCountedClass {{{1
struct RefCountedClass {
 public:
  RefCountedClass() {
    annotate_unref_ = false;
    ref_ = 0;
    data_ = 0;
  }

  ~RefCountedClass() {
    CHECK(ref_ == 0);     // race may be reported here 
    int data_val = data_; // and here     
                          // if MU is not annotated
    data_ = 0;
    ref_ = -1;
    printf("\tRefCountedClass::data_ = %d\n", data_val);
  }

  void AccessData() {
    this->mu_.Lock();
    this->data_++;
    this->mu_.Unlock();
  }

  void Ref() {
    MU.Lock();
    CHECK(ref_ >= 0);
    ref_++;
    MU.Unlock();
  }

  void Unref() {
    MU.Lock();
    ref_--;
    bool do_delete = ref_ == 0;
    if (annotate_unref_) {
      ANNOTATE_CONDVAR_SIGNAL(this);
    }
    MU.Unlock();
    if (do_delete) {
      if (annotate_unref_) {
        ANNOTATE_CONDVAR_WAIT(this);
      }
      delete this;
    } 
  }

  static void Annotate_MU() {
    ANNOTATE_MUTEX_IS_USED_AS_CONDVAR(&MU);
  }
  void AnnotateUnref() {
    annotate_unref_ = true;
  }
  void Annotate_Race() {
    ANNOTATE_BENIGN_RACE(&this->data_, "needs annotation");
    ANNOTATE_BENIGN_RACE(&this->ref_, "needs annotation");
  }
 private: 
  bool annotate_unref_;

  int data_;
  Mutex mu_; // protects data_ 

  int ref_; 
  static Mutex MU; // protects ref_
};

Mutex RefCountedClass::MU;

// test76: FP. Ref counting, no annotations. {{{1
namespace test76 {
int     GLOB = 0;
Barrier barrier(4);
RefCountedClass *object = NULL; 
void Worker() {
  object->Ref();
  barrier.Block();
  object->AccessData();
  object->Unref();
}
void Run() {
  printf("test76: false positive (ref counting)\n");
  object = new RefCountedClass; 
  object->Annotate_Race();
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 76, FEATURE)
}  // namespace test76



// test77: TN. Ref counting, MU is annotated. {{{1
namespace test77 {
// same as test76, but RefCountedClass::MU is annotated. 
int     GLOB = 0;
Barrier barrier(4);
RefCountedClass *object = NULL; 
void Worker() {
  object->Ref();
  barrier.Block();
  object->AccessData();
  object->Unref();
}
void Run() {
  printf("test77: true negative (ref counting), mutex is annotated\n");
  RefCountedClass::Annotate_MU();
  object = new RefCountedClass; 
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 77)
}  // namespace test77



// test78: TN. Ref counting, Unref is annotated. {{{1
namespace test78 {
// same as test76, but RefCountedClass::Unref is annotated. 
int     GLOB = 0;
Barrier barrier(4);
RefCountedClass *object = NULL; 
void Worker() {
  object->Ref();
  barrier.Block();
  object->AccessData();
  object->Unref();
}
void Run() {
  printf("test78: true negative (ref counting), Unref is annotated\n");
  RefCountedClass::Annotate_MU();
  object = new RefCountedClass; 
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 78)
}  // namespace test78



// test79 TN. Swap. {{{1
namespace test79 {
__gnu_cxx::hash_map<int, int> MAP;

// Here we use swap to pass hash_map between threads.
// The synchronization is correct, but w/o ANNOTATE_MUTEX_IS_USED_AS_CONDVAR
// Helgrind will complain.

void Worker1() {
  __gnu_cxx::hash_map<int, int> tmp;
  MU.Lock();
  // We swap the new empty map 'tmp' with 'MAP'.
  MAP.swap(tmp); 
  MU.Unlock();
  // tmp (which is the old version of MAP) is destroyed here.
}

void Worker2() {
  MU.Lock();
  MAP[1]++;  // Just update MAP under MU.
  MU.Unlock();
}

void Worker3() { Worker1(); } 
void Worker4() { Worker2(); } 

void Run() {
  ANNOTATE_MUTEX_IS_USED_AS_CONDVAR(&MU);
  printf("test79: negative\n");
  MyThreadArray t(Worker1, Worker2, Worker3, Worker4);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 79)
}  // namespace test79


// AtomicRefCountedClass. {{{1
// Same as RefCountedClass, but using atomic ops instead of mutex.
struct AtomicRefCountedClass {
 public:
  AtomicRefCountedClass() {
    annotate_unref_ = false;
    ref_ = 0;
    data_ = 0;
  }

  ~AtomicRefCountedClass() {
    CHECK(ref_ == 0);     // race may be reported here 
    int data_val = data_; // and here     
    data_ = 0;
    ref_ = -1;
    printf("\tRefCountedClass::data_ = %d\n", data_val);
  }

  void AccessData() {
    this->mu_.Lock();
    this->data_++;
    this->mu_.Unlock();
  }

  void Ref() {
    __sync_add_and_fetch(&ref_, 1);
  }

  void Unref() {
    // DISCLAIMER: I am not sure I've implemented this correctly
    // (might require some memory barrier, etc).
    // But this implementation of reference counting is enough for 
    // the purpose of Helgrind demonstration.
    __sync_sub_and_fetch(&ref_, 1);
    if (annotate_unref_) { ANNOTATE_CONDVAR_SIGNAL(this); }
    if (ref_ == 0) {
      if (annotate_unref_) { ANNOTATE_CONDVAR_WAIT(this); }
      delete this;
    } 
  }

  void AnnotateUnref() {
    annotate_unref_ = true;
  }
  void Annotate_Race() {
    ANNOTATE_BENIGN_RACE(&this->data_, "needs annotation");
  }
 private: 
  bool annotate_unref_;

  Mutex mu_; 
  int data_; // under mu_

  int ref_;  // used in atomic ops.
};

// test80: FP. Ref counting with atomics, no annotations. {{{1
namespace test80 {
int     GLOB = 0;
Barrier barrier(4);
AtomicRefCountedClass *object = NULL; 
void Worker() {
  object->Ref();
  barrier.Block();
  object->AccessData();
  object->Unref(); // All the tricky stuff is here.
}
void Run() {
  printf("test80: false positive (ref counting)\n");
  object = new AtomicRefCountedClass; 
  object->Annotate_Race();
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 80, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test80


// test81: TN. Ref counting with atomics, Unref is annotated. {{{1
namespace test81 {
// same as test80, but Unref is annotated.
int     GLOB = 0;
Barrier barrier(4);
AtomicRefCountedClass *object = NULL; 
void Worker() {
  object->Ref();
  barrier.Block();
  object->AccessData();
  object->Unref(); // All the tricky stuff is here.
}
void Run() {
  printf("test81: negative (annotated ref counting)\n");
  object = new AtomicRefCountedClass; 
  object->AnnotateUnref();
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 81, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test81


// test82: Object published w/o synchronization. {{{1
namespace test82 {

// Writer creates a new object and makes the pointer visible to the Reader.
// Reader waits until the object pointer is non-null and reads the object. 
//
// On Core 2 Duo this test will sometimes (quite rarely) fail in 
// the CHECK below, at least if compiled with -O2.
//
// The sequence of events:: 
// Thread1:                  Thread2: 
//   a. arr_[...] = ...
//   b. foo[i]    = ...
//                           A. ... = foo[i]; // non NULL
//                           B. ... = arr_[...]; 
// 
//  Since there is no proper synchronization, during the even (B)
//  Thread2 may not see the result of the event (a). 
//

class FOO {
 public: 
  FOO() {
    idx_ = rand() % 1024;
    arr_[idx_] = 77777;
  }
  static void check(volatile FOO *foo) {
    CHECK(foo->arr_[foo->idx_] == 77777);
  }
 private:
  int idx_;
  int arr_[1024];
};

const int N = 10000;
static volatile FOO *foo[N];

void Writer() {
  for (int i = 0; i < N; i++) {
    foo[i] = new FOO;
    usleep(100);
  }
}

void Reader() {
  for (int i = 0; i < N; i++) {
    while (!foo[i]) {
      MU.Lock();   // this is NOT a synchronization, 
      MU.Unlock(); // it just helps foo[i] to become visible in Reader.
    }
    if ((i % 100) == 0) {
      printf("rd %d\n", i);
    }
    // At this point Reader() sees the new value of foo[i] 
    // but in very rare cases will not see the new value of foo[i]->arr_.
    // Thus this CHECK will sometimes fail.
    FOO::check(foo[i]);
  }
}

void Run() {
  printf("test82: positive\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 82, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test82


// test83: Object published w/o synchronization (simple version){{{1
namespace test83 {
// A simplified version of test83 (example of a wrong code).
// This test, though incorrect, will almost never fail.
volatile static int *ptr = NULL;

void Writer() {
  usleep(100);
  ptr = new int(777);
}

void Reader() {
  while(!ptr) {
    MU.Lock(); // Not a synchronization!
    MU.Unlock(); 
  }
  CHECK(*ptr == 777);
}

void Run() {
//  printf("test83: positive\n");
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 83, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test83


// test84: TP. True race (regression test for a bug related to atomics){{{1
namespace test84 {
// Helgrind should not create HB arcs for the bus lock even when 
// --pure-happens-before=yes is used.
// Bug found in by Bart Van Assche, the test is taken from 
// valgrind file drd/tests/atomic_var.c.
static int s_x = 0;
/* s_dummy[] ensures that s_x and s_y are not in the same cache line. */
static char s_dummy[512] = {0};
static int s_y = 0;

void thread_func_1()
{
  s_y = 1;
  __sync_add_and_fetch(&s_x, 1);
}

void thread_func_2()
{
  while (__sync_add_and_fetch(&s_x, 0) == 0)
    ;
  printf("y = %d\n", s_y);
}


void Run() {
  CHECK(s_dummy[0] == 0);  // Avoid compiler warning about 's_dummy unused'.
  printf("test84: positive\n");
  ANNOTATE_EXPECT_RACE(&s_y, "TP. true race.");
  MyThreadArray t(thread_func_1, thread_func_2);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 84)
}  // namespace test84


// test85: Test for RunningOnValgrind(). {{{1
namespace  test85 {
int     GLOB = 0;
void Run() {
  printf("test85: RunningOnValgrind() = %d\n", RunningOnValgrind());
}
REGISTER_TEST(Run, 85)
}  // namespace test85


// test86: Test for race inside DTOR: racey write to vptr. Benign. {{{1
namespace test86 {
// This test shows a racey access to vptr (the pointer to vtbl).
// We have class A and class B derived from A. 
// Both classes have a virtual function f() and a virtual DTOR.
// We create an object 'A *a = new B'
// and pass this object from Thread1 to Thread2.
// Thread2 calls a->f(). This call reads a->vtpr.
// Thread1 deletes the object. B::~B waits untill the object can be destroyed 
// (flag_stopped == true) but at the very beginning of B::~B 
// a->vptr is written to. 
// So, we have a race on a->vptr. 
// On this particular test this race is benign, but test87 shows 
// how such race could harm.
//
//
// 
// Threa1:                                            Thread2: 
// 1. A a* = new B;                                  
// 2. Q.Put(a); ------------\                         .            
//                           \-------------------->   a. a = Q.Get();
//                                                    b. a->f();
//                                       /---------   c. flag_stopped = true;
// 3. delete a;                         /
//    waits untill flag_stopped <------/
//    inside the dtor
// 

bool flag_stopped = false;
Mutex mu;

ProducerConsumerQueue Q(INT_MAX);  // Used to pass A* between threads.

struct A {
  A()  { printf("A::A()\n"); }
  virtual ~A() { printf("A::~A()\n"); }
  virtual void f() { }
};

struct B: A {
  B()  { printf("B::B()\n"); }
  virtual ~B() { 
    // The race is here.    <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    printf("B::~B()\n"); 
    // wait until flag_stopped is true.
    mu.LockWhen(Condition(&ArgIsTrue, &flag_stopped));
    mu.Unlock();
    printf("B::~B() done\n"); 
  }
  virtual void f() { }
};

void Waiter() {
  A *a = new B;
  ANNOTATE_EXPECT_RACE(a, "expected race on a->vptr");
  printf("Waiter: B created\n");
  Q.Put(a);
  usleep(100000); // so that Worker calls a->f() first.
  printf("Waiter: deleting B\n");
  delete a;
  printf("Waiter: B deleted\n");
}

void Worker() {
  A *a = reinterpret_cast<A*>(Q.Get());
  printf("Worker: got A\n");
  a->f();
  printf("Worker: done\n");
    
  mu.Lock();
  flag_stopped = true;
  ANNOTATE_CONDVAR_SIGNAL(&mu);
  mu.Unlock();
}

void Run() {
  printf("test86: positive, race inside DTOR\n");
  MyThreadArray t(Waiter, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 86)
}  // namespace test86


// test87: Test for race inside DTOR: racey write to vptr. Harmful.{{{1
namespace test87 {
// A variation of test86 where the race is harmful.
// Here we have class C derived from B. 
// We create an object 'A *a = new C' in Thread1 and pass it to Thread2.
// Thread2 calls a->f(). 
// Thread1 calls 'delete a'. 
// It first calls C::~C, then B::~B where it rewrites the vptr to point 
// to B::vtbl. This is a problem because Thread2 might not have called a->f() 
// and now it will call B::f instead of C::f.
//
bool flag_stopped = false;
Mutex mu;

ProducerConsumerQueue Q(INT_MAX);  // Used to pass A* between threads.

struct A {
  A()  { printf("A::A()\n"); }
  virtual ~A() { printf("A::~A()\n"); }
  virtual void f() = 0; // pure virtual.
};

struct B: A {
  B()  { printf("B::B()\n"); }
  virtual ~B() { 
    // The race is here.    <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    printf("B::~B()\n"); 
    // wait until flag_stopped is true.
    mu.LockWhen(Condition(&ArgIsTrue, &flag_stopped));
    mu.Unlock();
    printf("B::~B() done\n"); 
  }
  virtual void f() = 0; // pure virtual.
};

struct C: B {
  C()  { printf("C::C()\n"); }
  virtual ~C() { printf("C::~C()\n"); }
  virtual void f() { }
};

void Waiter() {
  A *a = new C;
  Q.Put(a);
  delete a;
}

void Worker() {
  A *a = reinterpret_cast<A*>(Q.Get());
  a->f();
    
  mu.Lock();
  flag_stopped = true;
  ANNOTATE_CONDVAR_SIGNAL(&mu);
  mu.Unlock();
}

void Run() {
  printf("test87: positive, race inside DTOR\n");
  MyThreadArray t(Waiter, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST2(Run, 87, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test87


// test88: Test for ANNOTATE_IGNORE_WRITES_*{{{1
namespace test88 {
// a recey write annotated with ANNOTATE_IGNORE_WRITES_BEGIN/END.
int     GLOB = 0;
void Worker() {
  ANNOTATE_IGNORE_WRITES_BEGIN();
  GLOB = 1;
  ANNOTATE_IGNORE_WRITES_END();
}
void Run() {
  printf("test88: negative, test for ANNOTATE_IGNORE_WRITES_*\n");
  MyThread t(Worker);
  t.Start();
  GLOB = 1;
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 88)
}  // namespace test88


// test89: Test for debug info. {{{1
namespace test89 {
// Simlpe races with different objects (stack, heap globals; scalars, structs).
// Also, if run with --trace-level=2 this test will show a sequence of 
// CTOR and DTOR calls.
struct STRUCT {
  int a, b, c;
};

struct A {
  int a;  
  A() { 
    ANNOTATE_TRACE_MEMORY(&a);
    a = 1; 
  } 
  virtual ~A() {
    a = 4;
  }
}; 

struct B : A {  
  B()  { CHECK(a == 1); } 
  virtual ~B() { CHECK(a == 3); }
};
struct C : B {
  C()  { a = 2; }
  virtual ~C() { a = 3; } 
};

int            GLOBAL = 0;
int           *STACK  = 0;
STRUCT         GLOB_STRUCT;
STRUCT        *STACK_STRUCT;
STRUCT        *HEAP_STRUCT;

void Worker() {
  GLOBAL = 1;
  *STACK = 1;
  GLOB_STRUCT.b   = 1;
  STACK_STRUCT->b = 1;
  HEAP_STRUCT->b  = 1;
}

void Run() {
  int stack_var = 0;
  STACK = &stack_var;

  STRUCT stack_struct;
  STACK_STRUCT = &stack_struct;

  HEAP_STRUCT = new STRUCT;

  printf("test89: negative\n");
  MyThreadArray t(Worker, Worker);
  t.Start();
  t.Join();

  delete HEAP_STRUCT;

  A *a = new C;
  printf("Using 'a->a':  %d\n", a->a);
  delete a;
}
REGISTER_TEST2(Run, 89, FEATURE|EXCLUDE_FROM_ALL)
}  // namespace test89


// test90: FP. Test for a safely-published pointer (read-only). {{{1
namespace test90 {
// The Publisher creates an object and safely publishes it under a mutex.
// Readers access the object read-only.
// See also test91.
//
// Without annotations Helgrind will issue a false positive in Reader(). 
//
// Choices for annotations: 
//   -- ANNOTATE_CONDVAR_SIGNAL/ANNOTATE_CONDVAR_WAIT
//   -- ANNOTATE_MUTEX_IS_USED_AS_CONDVAR
//   -- ANNOTATE_PUBLISH_POINTER (not yet available).

int     *GLOB = 0;

void Publisher() {
  MU.Lock();
  GLOB = new int;
  *GLOB = 777;
  ANNOTATE_EXPECT_RACE(GLOB, "This is a false positve");
  MU.Unlock();
}

void Reader() {
  while (true) {
    MU.Lock();
    int *p = GLOB;
    MU.Unlock();
    if (p) {
      CHECK(*p == 777);  // Race is reported here.
      break;
    }
  }
}

void Run() {
  printf("test90: false positive (safely published pointer).\n");
  MyThreadArray t(Publisher, Reader, Reader, Reader);
  t.Start();
  t.Join();
  printf("\t*GLOB=%d\n", *GLOB);
}
REGISTER_TEST(Run, 90)
}  // namespace test90


// test91: FP. Test for a safely-published pointer (read-write). {{{1
namespace test91 {
// Similar to test90.
// The Publisher creates an object and safely publishes it under a mutex MU1.
// Accessors get the object under MU1 and access it (read/write) under MU2.
//
// Without annotations Helgrind will issue a false positive in Accessor(). 

int     *GLOB = 0;

void Publisher() {
  MU1.Lock();
  GLOB = new int;
  *GLOB = 777;
  ANNOTATE_EXPECT_RACE(GLOB, "This is a false positve");
  MU1.Unlock();
}

void Accessor() {
  while (true) {
    MU1.Lock();
    int *p = GLOB;
    MU1.Unlock();
    if (p) {
      MU2.Lock();
      (*p)++;  // Race is reported here.
      CHECK(*p >  777);  
      MU2.Unlock();
      break;
    }
  }
}

void Run() {
  printf("test91: false positive (safely published pointer, read/write).\n");
  MyThreadArray t(Publisher, Accessor, Accessor, Accessor);
  t.Start();
  t.Join();
  printf("\t*GLOB=%d\n", *GLOB);
}
REGISTER_TEST(Run, 91)
}  // namespace test91


// test300: {{{1
namespace test300 {
int     GLOB = 0;
void Run() {
}
REGISTER_TEST2(Run, 300, RACE_DEMO)
}  // namespace test300

// test301: Simple race.  {{{1
namespace test301 {
int     GLOB = 0;

Mutex MU1;
Mutex MU2;
void Worker1() { MU1.Lock(); GLOB=1; MU1.Unlock(); }
void Worker2() { MU2.Lock(); GLOB=1; MU2.Unlock(); }

void Run() {  
  printf("test301: simple race.\n");
  MyThread t1(Worker1), t2(Worker2);
  t1.Start();  
  t2.Start();  
  t1.Join();   t2.Join();
}
REGISTER_TEST2(Run, 301, RACE_DEMO)
}  // namespace test301

// test302: Complex race which happens at least twice.  {{{1
namespace test302 {
// In this test we have many different accesses to GLOB and only one access 
// is not synchronized properly. 
int     GLOB = 0;

Mutex MU1;
Mutex MU2;
void Worker() {
  for(int i = 0; i < 100; i++) {
    switch(i % 4) {
      case 0:
        // This read is protected correctly. 
        MU1.Lock(); CHECK(GLOB >= 0); MU1.Unlock();
        break;
      case 1:
        // Here we used the wrong lock! The reason of the race is here. 
        MU2.Lock(); CHECK(GLOB >= 0); MU2.Unlock();
        break;
      case 2:
        // This read is protected correctly. 
        MU1.Lock(); CHECK(GLOB >= 0); MU1.Unlock();
        break;
      case 3:
        // This write is protected correctly. 
        MU1.Lock(); GLOB++; MU1.Unlock();
        break;
    }
    // sleep a bit so that the threads interleave 
    // and the race happens at least twice. 
    usleep(100); 
  }
}

void Run() {  
  printf("test302: Complex race that happens twice.\n");
  MyThread t1(Worker), t2(Worker);
  t1.Start();  
  t2.Start();  
  t1.Join();   t2.Join();
}
REGISTER_TEST2(Run, 302, RACE_DEMO)
}  // namespace test302


// test303: Need to trace the memory to understand the report. {{{1
namespace test303 {
int     GLOB = 0;

Mutex MU;
void Worker1() { CHECK(GLOB >= 0); }
void Worker2() { MU.Lock(); GLOB=1;  MU.Unlock();}

void Run() {  
  printf("test303: a race that needs annotations.\n");
  ANNOTATE_TRACE_MEMORY(&GLOB);
  MyThreadArray t(Worker1, Worker2);
  t.Start();  
  t.Join(); 
}
REGISTER_TEST2(Run, 303, RACE_DEMO)
}  // namespace test303



// test304: Can not trace the memory, since it is a library object. {{{1
namespace test304 {
string *STR;

void Worker1() {
  sleep(0); 
  ANNOTATE_CONDVAR_SIGNAL((void*)0xDEADBEAF);
  MU.Lock(); CHECK(STR->length() >= 4); MU.Unlock();
}
void Worker2() {
  sleep(1);            
  ANNOTATE_CONDVAR_SIGNAL((void*)0xDEADBEAF);
  CHECK(STR->length() >= 4); // Unprotected! 
}
void Worker3() {
  sleep(2); 
  ANNOTATE_CONDVAR_SIGNAL((void*)0xDEADBEAF);
  MU.Lock(); CHECK(STR->length() >= 4); MU.Unlock();
}
void Worker4() {
  sleep(3); 
  ANNOTATE_CONDVAR_SIGNAL((void*)0xDEADBEAF);
  MU.Lock(); *STR += " + a very very long string"; MU.Unlock();  
}

void Run() {  
  STR = new string ("The String");
  printf("test304: a race where memory tracing does not work.\n");
  MyThreadArray t(Worker1, Worker2, Worker3, Worker4);
  t.Start();  
  t.Join(); 

  printf("%s\n", STR->c_str());
  delete STR;
}
REGISTER_TEST2(Run, 304, RACE_DEMO)
}  // namespace test304



// test305: A bit more tricky: two locks used inconsistenly. {{{1
namespace test305 {
int     GLOB = 0;

// In this test GLOB is protected by MU1 and MU2, but inconsistently.
// The TRACES observed by helgrind are: 
// TRACE[1]: Access{T2/S2 wr} -> new State{Mod; #LS=2; #SS=1; T2/S2}
// TRACE[2]: Access{T4/S9 wr} -> new State{Mod; #LS=1; #SS=2; T2/S2, T4/S9}
// TRACE[3]: Access{T5/S13 wr} -> new State{Mod; #LS=1; #SS=3; T2/S2, T4/S9, T5/S13}
// TRACE[4]: Access{T6/S19 wr} -> new State{Mod; #LS=0; #SS=4; T2/S2, T4/S9, T5/S13, T6/S19}
//
// The guilty access is either Worker2() or Worker4(), depending on 
// which mutex is supposed to protect GLOB.
Mutex MU1;
Mutex MU2;
void Worker1() { MU1.Lock(); MU2.Lock(); GLOB=1; MU2.Unlock(); MU1.Unlock(); }
void Worker2() { MU1.Lock();             GLOB=2;               MU1.Unlock(); }
void Worker3() { MU1.Lock(); MU2.Lock(); GLOB=3; MU2.Unlock(); MU1.Unlock(); }
void Worker4() {             MU2.Lock(); GLOB=4; MU2.Unlock();               }

void Run() {  
  ANNOTATE_TRACE_MEMORY(&GLOB);
  printf("test305: simple race.\n");
  MyThread t1(Worker1), t2(Worker2), t3(Worker3), t4(Worker4);
  t1.Start(); usleep(100);
  t2.Start(); usleep(100); 
  t3.Start(); usleep(100);
  t4.Start(); usleep(100);
  t1.Join(); t2.Join(); t3.Join(); t4.Join();
}
REGISTER_TEST2(Run, 305, RACE_DEMO)
}  // namespace test305

// test350: Simple race with deep stack. {{{1
namespace test350 {
int     GLOB = 0;

void F9() {GLOB++;}
void F8() {F9();}
void F7() {F8();}
void F6() {F7();}
void F5() {F6();}
void F4() {F5();}
void F3() {F4();}
void F2() {F3();}
void F1() {F2();}
void Worker() { F1();}

void Run() {  
  printf("test350: simple race with deep stack.\n");
  MyThread t1(Worker), t2(Worker);
  t1.Start();  
  t2.Start();  
  t1.Join();   t2.Join();
}
REGISTER_TEST2(Run, 350, RACE_DEMO)

}  // namespace test350

// test501: Manually call PRINT_* annotations {{{1
namespace test501 {
int  COUNTER = 0;
int     GLOB = 0;
Mutex muCounter, muGlob[65];

void Worker() {
   muCounter.Lock();
   int myId = ++COUNTER;
   muCounter.Unlock();
  
   usleep(100);
  
   muGlob[myId].Lock();
   muGlob[0].Lock();
   GLOB++;
   muGlob[0].Unlock();
   muGlob[myId].Unlock();
}

void Worker_1() {
   MyThreadArray ta (Worker, Worker, Worker, Worker);
   ta.Start();
   usleep(500000);
   ta.Join ();   
}

void Worker_2() {
   MyThreadArray ta (Worker_1, Worker_1, Worker_1, Worker_1);
   ta.Start();
   usleep(300000);
   ta.Join ();   
}

void Run() {
   ANNOTATE_RESET_STATS();
   printf("test501: Manually call PRINT_* annotations.\n");
   MyThreadArray ta (Worker_2, Worker_2, Worker_2, Worker_2);
   ta.Start();
   usleep(100000);
   ta.Join ();
   ANNOTATE_PRINT_MEMORY_USAGE(0);
   ANNOTATE_PRINT_STATS();
}

REGISTER_TEST2(Run, 501, FEATURE | EXCLUDE_FROM_ALL)
}  // namespace test501

// test502: produce lots of segments without cross-thread relations {{{1
namespace test502 {

/*
 * This test produces ~1Gb of memory usage when run with the following options:
 * 
 * --tool=helgrind
 * --trace-after-race=0
 * --num-callers=2
 * --more-context=no
 */

Mutex MU;
int     GLOB = 0;

void TP() {
   for (int i = 0; i < 750000; i++) {
      MU.Lock();
      GLOB++;
      MU.Unlock();
   }
}

void Run() {
   MyThreadArray t(TP, TP);
   printf("test502: produce lots of segments without cross-thread relations\n");
   
   t.Start();
   t.Join();
}

REGISTER_TEST2(Run, 502, MEMORY_USAGE | PRINT_STATS | EXCLUDE_FROM_ALL)
}  // namespace test502

// test503: produce lots of segments with simple HB-relations {{{1
// HB cache-miss rate is ~55%
namespace test503 {

//  |- |  |  |  |  |
//  | \|  |  |  |  |
//  |  |- |  |  |  |
//  |  | \|  |  |  |
//  |  |  |- |  |  |
//  |  |  | \|  |  |
//  |  |  |  |- |  |
//  |  |  |  | \|  |
//  |  |  |  |  |- |
//  |  |  |  |  | \|
//  |  |  |  |  |  |----
//->|  |  |  |  |  |
//  |- |  |  |  |  |
//  | \|  |  |  |  |
//     ...
  
const int N_threads = 32;
int       GLOB = 0;
ProducerConsumerQueue *Q[N_threads];
int GLOB_limit = 1000;

bool end = false;
int count = 0;
Mutex count_mu;

void Worker(){
   count_mu.Lock();
   int myId = count;
   count++;
   count_mu.Unlock();
   
   ProducerConsumerQueue &myQ = *Q[myId], &nextQ = *Q[(myId+1) % N_threads];
   
   // this code produces a new SS with each new segment
   while (true) {
      myQ.Get();
      if (end)
         break;
      GLOB++;
      if (GLOB % 100 == 0)
         printf("%8i\n", GLOB);
      
      if (myId == 0 && GLOB > GLOB_limit) {
         end = true;
         for (int i = 1; i < N_threads; i++)
            Q[i]->Put(NULL);
         break;
      } else
         nextQ.Put(NULL);
   }
}

void Run() {
   printf("test503: produce lots of segments with simple HB-relations\n");
   for (int i = 0; i < N_threads; i++)
      Q[i] = new ProducerConsumerQueue(1);
   Q[0]->Put(NULL);
   
   
   {      
      ThreadPool pool(N_threads);
      pool.StartWorkers();
      for (int i = 0; i < N_threads; i++) {
         pool.Add(NewCallback(Worker));
      }
   } // all folks are joined here.
   
   for (int i = 0; i < N_threads; i++)
      delete Q[i];
}

REGISTER_TEST2(Run, 503, MEMORY_USAGE | PRINT_STATS
                  | PERFORMANCE | EXCLUDE_FROM_ALL)
}  // namespace test503

// test504: force massive cache fetch-wback (50% misses, mostly CacheLineZ) {{{1
namespace test504 {

const int N_THREADS = 2,
          HG_CACHELINE_COUNT = 1 << 16,
          HG_CACHELINE_SIZE  = 1 << 6,
          HG_CACHE_SIZE = HG_CACHELINE_COUNT * HG_CACHELINE_SIZE;

// int gives us ~4x speed of the byte test
// 4x array size gives us
// total multiplier of 16x over the cachesize 
// so we can neglect the cached-at-the-end memory 
const int ARRAY_SIZE = 4 * HG_CACHE_SIZE;
int array[ARRAY_SIZE];

int count = 0;
Mutex count_mu;

void Worker() {
   count_mu.Lock();
   int myId = ++count;
   count_mu.Unlock();
   
   // all threads write to different memory locations,
   // so no synchronization mechanisms are needed
   int lower_bound = ARRAY_SIZE * (myId-1) / N_THREADS,
       upper_bound = ARRAY_SIZE * ( myId ) / N_THREADS;
   for (int i = lower_bound; i < upper_bound; 
            i += HG_CACHELINE_SIZE / sizeof(array[0])) {
      array[i] = i; // each array-write generates a cache miss
   }
}

void Run() {
   printf("test504: force massive CacheLineZ fetch-wback\n");
   MyThreadArray t(Worker, Worker);
   t.Start();
   t.Join();
}

REGISTER_TEST2(Run, 504, PERFORMANCE | PRINT_STATS | EXCLUDE_FROM_ALL)
}  // namespace test504

// test505: force massive cache fetch-wback (60% misses) {{{1
// modification of test504 - more threads, byte accesses and lots of mutexes
// so it produces lots of CacheLineF misses (30-50% of CacheLineZ misses)
namespace test505 {

const int N_THREADS = 2,
          HG_CACHELINE_COUNT = 1 << 16,
          HG_CACHELINE_SIZE  = 1 << 6,
          HG_CACHE_SIZE = HG_CACHELINE_COUNT * HG_CACHELINE_SIZE;

const int ARRAY_SIZE = 4 * HG_CACHE_SIZE;
int array[ARRAY_SIZE];

int count = 0;
Mutex count_mu;

void Worker() {
   const int N_MUTEXES = 5;
   Mutex mu[N_MUTEXES];
   count_mu.Lock();
   int myId = ++count;
   count_mu.Unlock();
   
   // all threads write to different memory locations,
   // so no synchronization mechanisms are needed
   int lower_bound = ARRAY_SIZE * (myId-1) / N_THREADS,
       upper_bound = ARRAY_SIZE * ( myId ) / N_THREADS;
   for (int mutex_id = 0; mutex_id < N_MUTEXES; mutex_id++) {
      Mutex *m = & mu[mutex_id];
      m->Lock();
      for (int i = lower_bound + mutex_id, cnt = 0; 
               i < upper_bound;
               i += HG_CACHELINE_SIZE / sizeof(array[0]), cnt++) {
         array[i] = i; // each array-write generates a cache miss
      }
      m->Unlock();
   }
}

void Run() {
   printf("test505: force massive CacheLineF fetch-wback\n");
   MyThreadArray t(Worker, Worker);
   t.Start();
   t.Join();
}

REGISTER_TEST2(Run, 505, PERFORMANCE | PRINT_STATS | EXCLUDE_FROM_ALL)
}  // namespace test505

// test506: massive HB's using Barriers {{{1
// HB cache miss is ~40%
// segments consume 10x more memory than SSs
// modification of test39
namespace test506 {
#ifndef NO_BARRIER
// Same as test17 but uses Barrier class (pthread_barrier_t). 
int     GLOB = 0;
const int N_threads = 64,
          ITERATIONS = 100000;
Barrier *barrier[ITERATIONS];

void Worker() {
  for (int i = 0; i < ITERATIONS; i++) {
     MU.Lock();
     GLOB++;
     MU.Unlock();
     barrier[i]->Block();
  }
}
void Run() {
  printf("test506: massive HB's using Barriers\n");
  for (int i = 0; i < ITERATIONS; i++) {
     barrier[i] = new Barrier(N_threads);
  }
  {
    ThreadPool pool(N_threads);
    pool.StartWorkers();
    for (int i = 0; i < N_threads; i++) {
      pool.Add(NewCallback(Worker));
    }
  } // all folks are joined here.
  CHECK(GLOB == N_threads * ITERATIONS);
  for (int i = 0; i < ITERATIONS; i++) {
     delete barrier[i];
  }
  printf("GLOB=%d\n", GLOB);
}
REGISTER_TEST2(Run, 506, PERFORMANCE | PRINT_STATS | EXCLUDE_FROM_ALL);
#endif // NO_BARRIER
}  // namespace test506

// test507: vgHelgrind_initIterAtFM/stackClear benchmark {{{1
// vgHelgrind_initIterAtFM/stackClear consume ~8.5%/5.5% CPU
namespace test507 {
const int N_THREADS    = 1,
          BUFFER_SIZE  = 1,
          ITERATIONS   = 1 << 20;

void Foo() {
  struct T {
    char temp;
    T() {
      ANNOTATE_RWLOCK_CREATE(&temp);
    }
    ~T() {
      ANNOTATE_RWLOCK_DESTROY(&temp);
    }
  } s[BUFFER_SIZE];
}

void Worker() {
  for (int j = 0; j < ITERATIONS; j++) {
    Foo();
  }
}

void Run() {
  printf("test507: vgHelgrind_initIterAtFM/stackClear benchmark\n");
  {
    ThreadPool pool(N_THREADS);
    pool.StartWorkers();
    for (int i = 0; i < N_THREADS; i++) {
      pool.Add(NewCallback(Worker));
    }
  } // all folks are joined here.
}
REGISTER_TEST2(Run, 507, PERFORMANCE/* | PRINT_STATS*/ | EXCLUDE_FROM_ALL);
}  // namespace test507

// test508: cmp_WordVecs_for_FM benchmark {{{1
// 50+% of CPU consumption by cmp_WordVecs_for_FM
namespace test508 {
const int N_THREADS    = 1,
          BUFFER_SIZE  = 1 << 10,
          ITERATIONS   = 1 << 9;

void Foo() {
  struct T {
    char temp;
    T() {
      ANNOTATE_RWLOCK_CREATE(&temp);
    }
    ~T() {
      ANNOTATE_RWLOCK_DESTROY(&temp);
    }
  } s[BUFFER_SIZE];
}

void Worker() {
  for (int j = 0; j < ITERATIONS; j++) {
    Foo();
  }
}

void Run() {
  printf("test508: cmp_WordVecs_for_FM benchmark\n");
  {
    ThreadPool pool(N_THREADS);
    pool.StartWorkers();
    for (int i = 0; i < N_THREADS; i++) {
      pool.Add(NewCallback(Worker));
    }
  } // all folks are joined here.
}
REGISTER_TEST2(Run, 508, PERFORMANCE/* | PRINT_STATS*/ | EXCLUDE_FROM_ALL);
}  // namespace test508

// test509: avl_find_node benchmark {{{1
// 10+% of CPU consumption by avl_find_node
namespace test509 {
const int N_THREADS    = 16,
          ITERATIONS   = 1 << 8;

void Worker() {
  std::vector<Mutex*> mu_list;
  for (int i = 0; i < ITERATIONS; i++) {
    Mutex * mu = new Mutex();
    mu_list.push_back(mu);
    mu->Lock();
  }
  for (int i = ITERATIONS - 1; i >= 0; i--) {
    Mutex * mu = mu_list[i];
    mu->Unlock();
    delete mu;
  }
}

void Run() {
  printf("test509: avl_find_node benchmark\n");
  {
    ThreadPool pool(N_THREADS);
    pool.StartWorkers();
    for (int i = 0; i < N_THREADS; i++) {
      pool.Add(NewCallback(Worker));
    }
  } // all folks are joined here.
}
REGISTER_TEST2(Run, 509, PERFORMANCE/* | PRINT_STATS*/ | EXCLUDE_FROM_ALL);
}  // namespace test509

// End {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:foldmethod=marker
