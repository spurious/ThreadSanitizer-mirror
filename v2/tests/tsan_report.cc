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
#include "tsan_test_util.h"
#include "tsan_defs.h"
#include "tsan_report.h"
#include "gtest/gtest.h"
#include <stddef.h>
#include <stdint.h>

TEST(ThreadSanitizer, ReportBasic) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read2(l);
  const ReportDesc *rep = t2.Write4(l, true);
  CHECK_NE(rep, NULL);
  CHECK_EQ(rep->typ, __tsan::ReportTypeRace);
  CHECK_EQ(rep->nmop, 2);
  CHECK_NE(rep->mop[0].tid, 0);
  CHECK_EQ(rep->mop[0].addr, (uintptr_t)l.loc());
  CHECK_EQ(rep->mop[0].size, 4);
  CHECK_EQ(rep->mop[0].write, true);
  CHECK_EQ(rep->mop[0].nmutex, 0);
  CHECK_EQ(rep->mop[0].stack.cnt, 1);
  CHECK_NE(rep->mop[0].stack.entry[0].pc, 0);
  CHECK_NE(rep->mop[0].stack.entry[0].func, NULL);
  CHECK_NE(rep->mop[0].stack.entry[0].file, NULL);
  CHECK_NE(rep->mop[0].stack.entry[0].line, 0);
  CHECK_NE(rep->mop[1].tid, 0);
  CHECK_NE(rep->mop[1].tid, rep->mop[0].tid);
  CHECK_EQ(rep->mop[1].addr, (uintptr_t)l.loc());
  CHECK_EQ(rep->mop[1].size, 2);
  CHECK_EQ(rep->mop[1].write, false);
  CHECK_EQ(rep->mop[1].nmutex, 0);
  CHECK_EQ(rep->mop[1].stack.cnt, 1);
  CHECK_NE(rep->mop[1].stack.entry[0].pc, 0);
  CHECK_NE(rep->mop[1].stack.entry[0].func, NULL);
  CHECK_NE(rep->mop[1].stack.entry[0].file, NULL);
  CHECK_NE(rep->mop[1].stack.entry[0].line, 0);
  CHECK_EQ(rep->loc, NULL);
  CHECK_EQ(rep->nthread, 0);
  CHECK_EQ(rep->nmutex, 0);
}
