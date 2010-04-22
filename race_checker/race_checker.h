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

// Author: Konstantin Serebryany
//
// RaceChecker helps confirming data races (e.g. those detected by Helgrind).
//
// Typical usage: if you suspect that there is a race in your code between
// foo() and bar() you instrument your code like this:
// Before instrumentation:
//   void foo() {
//     racey_object = ...;
//   }
//   void bar() {
//     ... = racey_object;
//   }
// After instrumentation:
//   void foo() {
//     RaceChecker checker(RaceChecker::WRITE, &racey_object);
//     racey_object = ...;
//   }
//   void bar() {
//     RaceChecker checker(RaceChecker::READ, &racey_object);
//     ... = racey_object;
//   }
//
// If the race actually happens at run-time  RaceChecker will print
// something like this to stderr:
//    Race found between these points
//    === writer:
//    ./a.out[0x40541a]
//    ./a.out[0x4054a7]
//    ./a.out[0x4054c3]
//    ./a.out[0x4054db]
//    ...
//    === reader:
//    ./a.out[0x40541a]
//    ./a.out[0x4054a7]
//    ./a.out[0x4054c3]
//    ...
//
// If you want to get the symbol names, filter the log through 'symbolize.py'
// which comes with race_checker.h (or any other similar script):
//    ./a.out[0x40541a] <FirstFunction>
//    ./a.out[0x4054a7] <SecondFunction>
//    ./a.out[0x4054c3] <ThirdFunction>
//    ...
//
// Nested calls to RaceChecker are allowed
//   {
//     RaceChecker read_checker(RaceChecker::READ, &racey_object);
//     // A code that reads racey_object.
//     if (want_to_write) {
//       RaceChecker write_checker(RaceChecker::WRITE, &racey_object);
//       // A code that writes racey_object.
//     }
//     // Another piece of code that reads racey_object.
//   }
//
//
// RaceChecker is controlled by the environment variable RACECHECKER
//   '0' or <empty>: disabled.
//   '1': just report races.
//   '2': report the first race and abort.
//
// The environment variable RACECHECKER_SLEEP_MS constrols the number of
// milliseconds to sleep in each call to RaceChecker.
// The more you sleep -- the better your chances to catch a race.
// Default value is 1 (sleep for a smallest positive time).
//
// If RaceChecker detected a race it is a 100% proof of a race.
// If it did not detect a race it proves nothing.
//
// Example: try running the unit test.
// % g++ -g race_checker.cc race_checker_unittest.cc -lpthread
// % ./a.out
// % RACECHECKER=1 ./a.out 2>&1 | head -19  | ./symbolize.py | c++filt
// Race found between these points
// === writer:
// ./a.out[0x40541a] <ReaderAndWriter()>
// ./a.out[0x4054a7] <Dummy3(void*)>
// ./a.out[0x4054c3] <Dummy2(void*)>
// ./a.out[0x4054db] <Dummy1(void*)>
// /lib64/tls/libpthread.so.0[0x2aaaaacc7f9f] <_fini>
// /lib64/tls/libc.so.6(clone+0x72)[0x2aaaab719ea2]
// === reader:
// ./a.out[0x4053ae] <ReaderAndWriter()>
// ./a.out[0x4054a7] <Dummy3(void*)>
// ./a.out[0x4054c3] <Dummy2(void*)>
// /lib64/tls/libpthread.so.0[0x2aaaaacc7f9f] <_fini>
// /lib64/tls/libc.so.6(clone+0x72)[0x2aaaab719ea2]
// === reader:
// ./a.out[0x4053ae] <ReaderAndWriter()>
// ./a.out[0x4054a7] <Dummy3(void*)>
// /lib64/tls/libpthread.so.0[0x2aaaaacc7f9f] <_fini>
// /lib64/tls/libc.so.6(clone+0x72)[0x2aaaab719ea2]
//


#ifndef RACE_CHECKER_H_
#define RACE_CHECKER_H_

#include <string>

#ifdef _MSC_VER
#include <windows.h>
#else
#include <stdint.h>
#include <pthread.h>
#endif

class RaceChecker {
 public:
  enum Type { READ = 0, WRITE = 1 };
  RaceChecker(Type type, const volatile void *address)
    : type_(type), id_(AddressToString(address)) {
    this->Start();
  }
  RaceChecker(Type type, const char *id)
    : type_(type), id_(id) {
    this->Start();
  }
  RaceChecker(Type type, const std::string &id)
    : type_(type), id_(id) {
    this->Start();
  }
  ~RaceChecker() {
    this->End();
  }
#ifdef _MSC_VER
  typedef HANDLE Thread;
  typedef DWORD  ThreadId;
#else
  typedef pthread_t Thread;
  typedef pthread_t ThreadId;
#endif
 private:
  std::string AddressToString(const volatile void *ptr) {
    char tmp[100] = "";
    sprintf(tmp, "0x%X", ptr);
    return tmp;
  }
  bool IdIsEmpty() {
    return id_ == "" || id_ == AddressToString(NULL);
  }

  void Start();
  void End();
  int type_;
  ThreadId thread_;
  std::string id_;
};

#endif  // RACE_CHECKER_H_
