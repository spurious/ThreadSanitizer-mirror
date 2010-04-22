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

#include <assert.h>
#include "race_checker.h"

int foo = 0;  // The racey address.

// Several reads and writes, nested calls to RaceChecker.
void ReaderAndWriter() {
  for (int i = 0; i < 1000; i++) {
    RaceChecker read_checker(RaceChecker::READ, &foo);
    assert(foo >= 0);  // Just a read.
    if ((i % 10) == 0) {
      RaceChecker write_checker(RaceChecker::WRITE, &foo);
      foo++;
    }
    assert(foo >= 0);  // Just a read.
  }
}

// More functions to make the stack traces more interesting.
static void *Dummy3(void *x) { ReaderAndWriter(); return NULL; }
static void *Dummy2(void *x) { return Dummy3(x); }
static void *Dummy1(void *x) { return Dummy2(x); }

RaceChecker::Thread StartThread(void* (*f)(void*)) {
  #ifdef _MSC_VER
  DWORD tmp;
  return ::CreateThread(0, NULL, (LPTHREAD_START_ROUTINE)f, 0, 0, 0);
  #else
  pthread_t ret;
  pthread_create(&ret, NULL, f, NULL);
  return ret;
  #endif
}

void JoinThread(RaceChecker::Thread t) {
  #ifdef _MSC_VER
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
  #else
  pthread_join(t, NULL);
  #endif
}

int main() {
  RaceChecker::Thread threads[3];
  threads[0] = StartThread(Dummy1);
  threads[1] = StartThread(Dummy2);
  threads[2] = StartThread(Dummy3);

  for (int i = 0; i < 3; i++)
    JoinThread(threads[i]);
}
