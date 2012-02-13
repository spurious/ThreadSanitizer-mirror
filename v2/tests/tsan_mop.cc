//===-- tsan_mop.cc ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "gtest/gtest.h"

TEST(ThreadSanitizer, SimpleWrite) {
  ScopedThread t;
  MemLoc l;
  t.Write1(l);
}

TEST(ThreadSanitizer, SimpleWriteWrite) {
  ScopedThread t1, t2;
  MemLoc l1, l2;
  t1.Write1(l1);
  t2.Write1(l2);
}

TEST(ThreadSanitizer, WriteWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Write1(l, true);
}

TEST(ThreadSanitizer, ReadWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Write1(l, true);
}

TEST(ThreadSanitizer, WriteReadRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Read1(l, true);
}

TEST(ThreadSanitizer, ReadReadNoRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Read1(l);
}

TEST(ThreadSanitizer, RaceWithOffset) {
  ScopedThread t1, t2;
  {
    MemLoc l;
    t1.Access(l.loc(), true, 8, false);
    t2.Access((char*)l.loc() + 4, true, 4, true);
  }
  {
    MemLoc l;
    t1.Access(l.loc(), true, 8, false);
    t2.Access((char*)l.loc() + 7, true, 1, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 4, true, 4, false);
    t2.Access((char*)l.loc() + 4, true, 2, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 4, true, 4, false);
    t2.Access((char*)l.loc() + 6, true, 2, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 3, true, 2, false);
    t2.Access((char*)l.loc() + 4, true, 1, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 1, true, 8, false);
    t2.Access((char*)l.loc() + 3, true, 1, true);
  }
}

TEST(ThreadSanitizer, NoRaceWithOffset) {
  ScopedThread t1, t2;
  {
    MemLoc l;
    t1.Access(l.loc(), true, 4, false);
    t2.Access((char*)l.loc() + 4, true, 4, false);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 3, true, 2, false);
    t2.Access((char*)l.loc() + 1, true, 2, false);
    t2.Access((char*)l.loc() + 5, true, 2, false);
  }
}
