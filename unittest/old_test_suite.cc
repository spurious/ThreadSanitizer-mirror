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

/* Author: Konstantin Serebryany <opensource@google.com>

 This file contains a simple test suite for some of our old unit-tests.
 These tests are likely to be moved to googletest framework over time.
*/

#include <algorithm>
#include <gtest/gtest.h>

#include "old_test_suite.h"

Mutex printf_mu;
std::map<int, Test> *TheMapOfTests = NULL;

#ifndef MAIN_INIT_ACTION
#define MAIN_INIT_ACTION
#endif

int ParseInt(const char *str) {
  int ret = 0;
  const char *cur = str;
  do {
    if (!isdigit(*cur)) {
      printf("\"%s\" is not a valid number\n", str);
      exit(1);
    }

    ret = ret*10 + (*cur - '0');
  } while (*(++cur));
  return ret;
}

class RandomGenerator {
 public:
  RandomGenerator(int seed) { srand(seed); }
  size_t operator( )(size_t n) const { return rand() % n; }
};

int main(int argc, char** argv) {
  MAIN_INIT_ACTION;
  testing::InitGoogleTest(&argc, argv);
  printf("FLAGS [phb=%i, fm=%i]\n", Tsan_PureHappensBefore(), Tsan_FastMode());

  std::vector<int> tests_to_run;
  std::set<int> tests_to_exclude;
  int shuffle_seed = 0;  // non-zero to shuffle.

  int id = 1;
  while (id < argc) {
    char *cur_arg = argv[id];
    if (!strcmp(cur_arg, "benchmark")) {
      for (std::map<int,Test>::iterator it = TheMapOfTests->begin();
        it != TheMapOfTests->end(); ++it) {
        if(it->second.flags_ & PERFORMANCE)
          tests_to_run.push_back(it->first);
      }
    } else if (!strcmp(cur_arg, "demo")) {
      for (std::map<int,Test>::iterator it = TheMapOfTests->begin();
        it != TheMapOfTests->end();  ++it) {
        if(it->second.flags_ & RACE_DEMO)
          tests_to_run.push_back(it->first);
      }
    } else if (!strncmp(cur_arg, "shuffle", 7)) {
      if (strlen(cur_arg) == 7) {
        shuffle_seed = GetTimeInMs();
        printf("Shuffling with seed = %i\n", shuffle_seed);
      } else {
        CHECK(cur_arg[7] == '=');
        shuffle_seed = ParseInt(cur_arg + 8);
      }
    } else {
      if (isdigit(cur_arg[0])) {
        // Enqueue the test specified.
        int test_id = ParseInt(cur_arg);
        CHECK(TheMapOfTests->count(test_id));
        tests_to_run.push_back(test_id);
      } else if (cur_arg[0] == '-') {
        // Exclude the test specified.
        int test_id = ParseInt(cur_arg + 1);
        CHECK(TheMapOfTests->count(test_id));
        tests_to_exclude.insert(test_id);
      } else {
        printf("Unknown argument: %s\n", cur_arg);
        exit(1);
      }
    }

    id++;
  }

  if (tests_to_run.size() == 0) {
    printf("No tests specified.\nRunning default set of tests...\n");
    for (std::map<int,Test>::iterator it = TheMapOfTests->begin();
        it != TheMapOfTests->end();
        ++it) {
      if(it->second.flags_ & EXCLUDE_FROM_ALL) continue;
      if(it->second.flags_ & RACE_DEMO) continue;
      tests_to_run.push_back(it->first);
    }
  }

  if (shuffle_seed > 0) {
    RandomGenerator rnd(shuffle_seed);
    random_shuffle(tests_to_run.begin(), tests_to_run.end(), rnd);
  }

  for (size_t i = 0; i < tests_to_run.size(); i++) {
    int test_id = tests_to_run[i];
    if (tests_to_exclude.count(test_id) > 0) {
      printf("test%i was excluded\n", test_id);
    } else {
      (*TheMapOfTests)[test_id].Run();
    }
  }

  return RUN_ALL_TESTS();
}
// End {{{1
 // vim:shiftwidth=2:softtabstop=2:expandtab:foldmethod=marker
