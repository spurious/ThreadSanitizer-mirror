//===-- tsan_report.cc ------------------------------------------*- C++ -*-===//
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
#include "tsan_defs.h"
#include "tsan_report.h"
#include "gtest/gtest.h"
#include <stddef.h>
#include <stdint.h>

using namespace __tsan;  // NOLINT

TEST(ThreadSanitizer, ReportBasic) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read2(l);
  const ReportDesc *rep = t2.Write4(l, true);
  EXPECT_NE(rep, (void*)0);
  EXPECT_EQ(rep->typ, __tsan::ReportTypeRace);
  EXPECT_EQ(rep->nmop, 2);
  EXPECT_NE(rep->mop[0].tid, 0);
  EXPECT_EQ(rep->mop[0].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[0].size, 4);
  EXPECT_EQ(rep->mop[0].write, true);
  EXPECT_EQ(rep->mop[0].nmutex, 0);
  EXPECT_EQ(rep->mop[0].stack.cnt, 1);
  EXPECT_NE(rep->mop[0].stack.entry[0].pc, (uptr)0);
  EXPECT_NE(rep->mop[0].stack.entry[0].func, (void*)0);
  EXPECT_NE(rep->mop[0].stack.entry[0].file, (void*)0);
  EXPECT_NE(rep->mop[0].stack.entry[0].line, 0);
  EXPECT_NE(rep->mop[1].tid, 0);
  EXPECT_NE(rep->mop[1].tid, rep->mop[0].tid);
  EXPECT_EQ(rep->mop[1].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[1].size, 2);
  EXPECT_EQ(rep->mop[1].write, false);
  EXPECT_EQ(rep->mop[1].nmutex, 0);
  EXPECT_EQ(rep->mop[1].stack.cnt, 1);
  EXPECT_NE(rep->mop[1].stack.entry[0].pc, (uptr)0);
  EXPECT_NE(rep->mop[1].stack.entry[0].func, (void*)0);
  EXPECT_NE(rep->mop[1].stack.entry[0].file, (void*)0);
  EXPECT_NE(rep->mop[1].stack.entry[0].line, 0);
  EXPECT_EQ(rep->loc, (void*)0);
  EXPECT_EQ(rep->nthread, 0);
  EXPECT_EQ(rep->nmutex, 0);
}

static void foo() {}; static int foo_line = __LINE__;  // NOLINT
static void bar() {}; static int bar_line = __LINE__;  // NOLINT

static uintptr_t NOINLINE get_pc() {
  return (uintptr_t)__builtin_return_address(0);
}

static int NOINLINE mop_no_inline(void *addr, uintptr_t *pc) {
  *pc = get_pc();
  __tsan_write1(addr); int line = __LINE__;  // NOLINT
  return line;
}

TEST(ThreadSanitizer, ReportStack) {
  ScopedThread t1;
  MemLoc l;
  uintptr_t pc = 0;
  int line = mop_no_inline(l.loc(), &pc);
  const char *file = __FILE__;
  t1.Call(&foo);
  t1.Call(&bar);
  const ReportDesc *rep = t1.Write1(l, true);
  EXPECT_EQ(rep->typ, __tsan::ReportTypeRace);
  EXPECT_EQ(rep->nmop, 2);
  EXPECT_NE(rep->mop[0].tid, 0);
  EXPECT_EQ(rep->mop[0].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[0].size, 1);
  EXPECT_EQ(rep->mop[0].write, true);
  EXPECT_EQ(rep->mop[0].stack.cnt, 3);
  EXPECT_EQ(rep->mop[0].stack.entry[1].pc, (uintptr_t)(void*)&bar);
  EXPECT_NE(strstr(rep->mop[0].stack.entry[1].func, "bar"), (void*)0);
  EXPECT_NE(strstr(rep->mop[0].stack.entry[1].file, file), (void*)0);
  EXPECT_EQ(rep->mop[0].stack.entry[1].line, bar_line);
  EXPECT_EQ(rep->mop[0].stack.entry[2].pc, (uintptr_t)(void*)&foo);
  EXPECT_NE(strstr(rep->mop[0].stack.entry[2].func, "foo"), (void*)0);
  EXPECT_NE(strstr(rep->mop[0].stack.entry[2].file, file), (void*)0);
  EXPECT_EQ(rep->mop[0].stack.entry[2].line, foo_line);
  EXPECT_EQ(rep->mop[1].tid, 0);
  EXPECT_EQ(rep->mop[1].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[1].size, 1);
  EXPECT_EQ(rep->mop[1].write, true);
  EXPECT_GE(rep->mop[1].stack.cnt, 1);
  EXPECT_GT(rep->mop[1].stack.entry[0].pc, pc - 64);
  EXPECT_LT(rep->mop[1].stack.entry[0].pc, pc + 64);
  EXPECT_NE(strstr(rep->mop[1].stack.entry[0].func, "mop_no_inline"), (void*)0);
  EXPECT_NE(strstr(rep->mop[1].stack.entry[0].file, file), (void*)0);
  EXPECT_GT(rep->mop[1].stack.entry[0].line, line - 3);
  EXPECT_LT(rep->mop[1].stack.entry[0].line, line + 3);
  // EXPECT_NE(strstr(rep->mop[1].stack.entry[1].func, "main"), (void*)0);
}

TEST(ThreadSanitizer, ReportDeadThread) {
  // Ensure that we can restore a stack of a finished thread.
  MemLoc l;
  ScopedThread t1;
  {
    ScopedThread t2;
    t2.Call(&foo);
    t2.Call(&bar);
    t2.Write1(l);
  }
  const ReportDesc *rep = t1.Write1(l, true);
  EXPECT_EQ(rep->mop[1].stack.cnt, 3);
}

struct ClassWithStatic {
  static int Data[4];
};

int ClassWithStatic::Data[4];

static void foobarbaz() {}

TEST(DISABLED_FAILS_ThreadSanitizer, ReportRace) {
  ScopedThread t1;
  MainThread().Access(&ClassWithStatic::Data, true, 4, false);
  t1.Call(&foobarbaz);
  t1.Access(&ClassWithStatic::Data, true, 2, false);
  t1.Return();
}
