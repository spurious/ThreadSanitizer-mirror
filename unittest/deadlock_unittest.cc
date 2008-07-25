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
// This file contains a set of unit tests for a deadlock detection tool. 
//
//
//
// This test can be compiled with pthreads (default) or
// with any other library that supports threads, locks, cond vars, etc. 
// 
// To compile with pthreads: 
//   g++  deadlock_unittest.cc -lpthread -g
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

#include <vector>
#include <string>
#include <map>
#include <ext/hash_map>
#include <algorithm>
#include <cstring>      // strlen(), index(), rindex()

//
// Each test resides in its own namespace. 
// Namespaces are named test01, test02, ... 
// Please, *DO NOT* change the logic of existing tests nor rename them. 
// Create a new test instead. 
//
// Some tests use sleep()/usleep(). 
// This is not a synchronization, but a simple way to trigger 
// some specific behaviour of the scheduler.

// Globals and utilities used by several tests. {{{1

typedef void (*void_func_void_t)(void);

struct Test{
  void_func_void_t f_;
  int flags_;
  Test(void_func_void_t f, int flags) 
    : f_(f)
    , flags_(flags)
  {}
  Test() : f_(0), flags_(0) {}
};
std::map<int, Test> TheMapOfTests;


struct TestAdder {
  TestAdder(void_func_void_t f, int id, int flags = 0) {
    CHECK(TheMapOfTests.count(id) == 0);
    TheMapOfTests[id] = Test(f, flags);
  }
};

#define REGISTER_TEST(f, id)         TestAdder add_test_##id (f, id);

// Put everything into stderr.
#define printf(args...) fprintf(stderr, args)

#ifndef MAIN_INIT_ACTION
#define MAIN_INIT_ACTION
#endif 


static bool ArgIsOne(int *arg) { return *arg == 1; };
static bool ArgIsZero(int *arg) { return *arg == 0; };
static bool ArgIsFalse(bool *arg) { return *arg == false; }
static bool ArgIsTrue(bool *arg) { return *arg == true; }


int main(int argc, char** argv) { // {{{1
  MAIN_INIT_ACTION;
  if (argc > 1) {
    // the tests are listed in command line flags 
    for (int i = 1; i < argc; i++) {
      int f_num = atoi(argv[i]);
      CHECK(TheMapOfTests.count(f_num));
      TheMapOfTests[f_num].f_();
    }
  } else {
    // all tests 
    for (std::map<int,Test>::iterator it = TheMapOfTests.begin(); 
        it != TheMapOfTests.end();
        ++it) {
      it->second.f_();
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
void Run() {
  printf("test00: negative\n");
}
REGISTER_TEST(Run, 00)
}  // namespace test00

// test01: Simple deadlock, 2 threads. {{{1
namespace test01 {
Mutex mu1, mu2;
void Worker1()  {
  mu1.Lock();
  mu2.Lock();
  mu2.Unlock();
  mu1.Unlock();
}
void Worker2()  {
  usleep(1000);
  mu2.Lock();
  mu1.Lock();
  mu1.Unlock();
  mu2.Unlock();
}
void Run() {
  MyThreadArray t(Worker1, Worker2);
  t.Start();
  t.Join();
  printf("test01: positive, simple deadlock\n");
}
REGISTER_TEST(Run, 01)
}  // namespace test01

// test02: Simple deadlock, 4 threads. {{{1
namespace test02 {
Mutex mu1, mu2, mu3, mu4;
void Worker1()  {
  mu1.Lock();   mu2.Lock();
  mu2.Unlock(); mu1.Unlock();
}
void Worker2()  {
  usleep(1000);
  mu2.Lock();   mu3.Lock();
  mu3.Unlock(); mu2.Unlock();
}
void Worker3()  {
  usleep(2000);
  mu3.Lock();   mu4.Lock();
  mu4.Unlock(); mu3.Unlock();
}
void Worker4()  {
  usleep(3000);
  mu4.Lock();   mu1.Lock();
  mu1.Unlock(); mu4.Unlock();
}
void Run() {
  MyThreadArray t(Worker1, Worker2, Worker3, Worker4);
  t.Start();
  t.Join();
  printf("test02: positive, simple deadlock\n");
}
REGISTER_TEST(Run, 02)
}  // namespace test02

// test03: Queue deadlock test (under construction). {{{1
namespace  test03 {

ProducerConsumerQueue Q[2] = {INT_MAX, INT_MAX};
bool cond[2] = {false, false};
Mutex mu[2];
int work_item[2] = {0, 0};

void PutAndWait(int idx) {
  // Put work_item1.
  Q[idx].Put(&work_item[idx]);

  // Wait for work_item1 completion.
  mu[idx].LockWhen(Condition(&ArgIsTrue, &cond[idx]));
  mu[idx].Unlock();
}

void GetAndSignal(int idx) {
  // Get an item.
  void *item = Q[idx].Get();

  // Signal item completion.
  mu[idx].Lock();
  *(reinterpret_cast<int*>(item)) = 1;
  cond[idx] = true;
  mu[idx].Unlock();

}

void Worker1() {
  PutAndWait(0);
  GetAndSignal(1);
}

void Worker2() {
  PutAndWait(1);
  GetAndSignal(0);
}

void Run() {
  printf("test03: queue deadlock\n");
  MyThreadArray t(Worker1, Worker2);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 03)
}  // namespace test03
