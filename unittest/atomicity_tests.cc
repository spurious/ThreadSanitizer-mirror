/* Copyright (c) 2008-2010, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// This file is a part of a test suite for ThreadSanitizer, a race detector.
// Author: Konstantin Serebryany.
//
// C++ tests for atomicity violations (aka high-level races).
// See also: http://code.google.com/p/data-race-test/wiki/HighLevelDataRaces
#include "test_utils.h"

#include <gtest/gtest.h>

namespace AtomicityTests {  // {{{1


// The most popular form of atomicity violation.
// Every method of a class is locked, but not every method is atomic.
// So,
//   if(v.size() > 0)
//     v.pop_back()
// may fail, if another thread called v.pop_back() in between.
class LockedVector {
 public:
  size_t size() {
    MutexLock l(&mu_);
    return v_.size();
  }

  void push_back(int a) {
    MutexLock l(&mu_);
    v_.push_back(a);
  }

  void pop_back() {
    MutexLock l(&mu_);
    v_.pop_back();
  }

 private:
  vector<int> v_;
  Mutex       mu_;
};

const int N = 10000;
LockedVector v;

void Worker() {
  for (int i = 0; i < N; i++) {
    if (v.size() > 0)
      v.pop_back();
    v.push_back(i);
  }
}

// The test is disabled because it actually fails sometimes.
// Run it with --gtest_also_run_disabled_tests
TEST(AtomicityTests, DISABLED_LockedVector) {
  MyThreadArray t(Worker, Worker);
  t.Start();
  t.Join();
}

}
