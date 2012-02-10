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
#include "tsan_report.h"
#include "tsan_rtl.h"

namespace __tsan {

static void PrintStack(const ReportStack *stack) {
  for (int i = 0; i < stack->cnt; i++) {
    const ReportStackEntry *ent = &stack->entry[i];
    Printf("    #%d %p: %s %s:%d\n", i,
           (void*)ent->pc, ent->func, ent->file, ent->line);
  }
}

void PrintReport(const ReportDesc *rep) {
  Printf("==================\n");
  Printf("WARNING: Data race\n");
  for (int i = 0; i < rep->nmop; i++) {
    const ReportMop *mop = &rep->mop[i];
    Printf("  %s of size %d at %p by thread %d:\n",
           (mop->write ? "Write" : "Read"),
           mop->size, (void*)mop->addr, mop->tid);
    PrintStack(&mop->stack);
  }
  if (rep->loc) {
    const ReportLocation *loc = rep->loc;
    if (loc->type == ReportLocationGlobal) {
      Printf("  Location is global %s of size %d at %p\n",
             loc->name, loc->size, loc->addr);
    } else if (loc->type == ReportLocationHeap) {
      Printf("  Location is heap of size %d at %p allocated by thread %d:\n",
             loc->size, loc->addr, loc->tid);
      PrintStack(&loc->stack);
    } else if (loc->type == ReportLocationStack) {
      Printf("  Location is stack of thread %d:\n", loc->tid);
    }
  }
  Printf("==================\n");
}

}  // namespace __tsan
