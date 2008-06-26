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

#include <pthread.h>
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

int main() {
  pthread_t threads[3];
  pthread_create(&threads[0], NULL, &Dummy1, NULL);
  pthread_create(&threads[1], NULL, &Dummy2, NULL);
  pthread_create(&threads[2], NULL, &Dummy3, NULL);

  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);
  pthread_join(threads[2], NULL);
}
