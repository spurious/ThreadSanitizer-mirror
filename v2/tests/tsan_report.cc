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
  ReportStack *stack = rep->mop[0].stack;
  EXPECT_NE(stack, (ReportStack*)0);
  EXPECT_EQ(stack->next, (ReportStack*)0);
  EXPECT_NE(stack->pc, (uptr)0);
  EXPECT_NE(stack->func, (void*)0);
  EXPECT_NE(stack->file, (void*)0);
  EXPECT_NE(stack->line, 0);
  EXPECT_NE(rep->mop[1].tid, 0);
  EXPECT_NE(rep->mop[1].tid, rep->mop[0].tid);
  EXPECT_EQ(rep->mop[1].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[1].size, 2);
  EXPECT_EQ(rep->mop[1].write, false);
  EXPECT_EQ(rep->mop[1].nmutex, 0);
  stack = rep->mop[1].stack;
  EXPECT_NE(stack, (ReportStack*)0);
  EXPECT_EQ(stack->next, (ReportStack*)0);
  EXPECT_NE(stack->pc, (uptr)0);
  EXPECT_NE(stack->func, (void*)0);
  EXPECT_NE(stack->file, (void*)0);
  EXPECT_NE(stack->line, 0);
  EXPECT_EQ(rep->loc, (void*)0);
  EXPECT_EQ(rep->nthread, 2);
  EXPECT_EQ(rep->nmutex, 0);
}

static void foo() { volatile int x = 42; (void)x; }; static int foo_line = __LINE__;  // NOLINT
static void bar() { volatile int x = 43; (void)x; }; static int bar_line = __LINE__;  // NOLINT

static uintptr_t NOINLINE get_pc() {
  return (uintptr_t)__builtin_return_address(0);
}

static int NOINLINE mop_no_inline(void *addr, uintptr_t *pc) {
  *pc = get_pc();
  __tsan_write1(addr); int line = __LINE__;  // NOLINT
  return line;
}

static bool contains(const char *str, const char *what) {
  return str ? strstr(str, what) != 0 : false;
}

TEST(ThreadSanitizer, ReportStack) {
  ScopedThread t1;
  MemLoc l;
  uintptr_t pc = 0;
  int line = mop_no_inline(l.loc(), &pc);
  const char *file = __FILE__;
  t1.Call((void(*)())((uintptr_t)(void*)&foo + 1));
  t1.Call((void(*)())((uintptr_t)(void*)&bar + 1));
  const ReportDesc *rep = t1.Write1(l, true);
  t1.Return();
  t1.Return();
  EXPECT_EQ(rep->typ, __tsan::ReportTypeRace);
  EXPECT_EQ(rep->nmop, 2);
  EXPECT_NE(rep->mop[0].tid, 0);
  EXPECT_EQ(rep->mop[0].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[0].size, 1);
  EXPECT_EQ(rep->mop[0].write, true);
  ReportStack *stack = rep->mop[0].stack;
  EXPECT_NE(stack, (ReportStack*)0);
  stack = stack->next;
  EXPECT_NE(stack, (ReportStack*)0);
  EXPECT_EQ(stack->pc, (uintptr_t)(void*)&bar + 1);
  EXPECT_TRUE(contains(stack->func, "bar"));
  EXPECT_TRUE(contains(stack->file, file));
  EXPECT_EQ(stack->line, bar_line);
  stack = stack->next;
  EXPECT_NE(stack, (ReportStack*)0);
  EXPECT_EQ(stack->pc, (uintptr_t)(void*)&foo + 1);
  EXPECT_TRUE(contains(stack->func, "foo"));
  EXPECT_TRUE(contains(stack->file, file));
  EXPECT_EQ(stack->line, foo_line);
  EXPECT_EQ(stack->next, (ReportStack*)0);
  EXPECT_EQ(rep->mop[1].tid, 0);
  EXPECT_EQ(rep->mop[1].addr, (uintptr_t)l.loc());
  EXPECT_EQ(rep->mop[1].size, 1);
  EXPECT_EQ(rep->mop[1].write, true);
  stack = rep->mop[1].stack;
  EXPECT_NE(stack, (ReportStack*)0);
  EXPECT_EQ(stack->next, (ReportStack*)0);
  EXPECT_GT(stack->pc, pc - 64);
  EXPECT_LT(stack->pc, pc + 64);
  EXPECT_TRUE(contains(stack->func, "mop_no_inline"));
  EXPECT_TRUE(contains(stack->file, file));
  EXPECT_GT(stack->line, line - 3);
  EXPECT_LT(stack->line, line + 3);
  // EXPECT_TRUE(contains(rep->mop[1].stack.entry[1].func, "main"));
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
  EXPECT_NE(rep->mop[1].stack, (ReportStack*)0);
  EXPECT_NE(rep->mop[1].stack->next, (ReportStack*)0);
  EXPECT_NE(rep->mop[1].stack->next->next, (ReportStack*)0);
  EXPECT_EQ(rep->mop[1].stack->next->next->next, (ReportStack*)0);
}

struct ClassWithStatic {
  static int Data[4];
};

int ClassWithStatic::Data[4];

static void foobarbaz() {}

TEST(ThreadSanitizer, ReportRace) {
  ScopedThread t1;
  MainThread().Access(&ClassWithStatic::Data, true, 4, false);
  t1.Call(&foobarbaz);
  const ReportDesc *rep = t1.Access(&ClassWithStatic::Data, true, 2, true);
  EXPECT_NE(rep, (ReportDesc*)0);
  t1.Return();
}
