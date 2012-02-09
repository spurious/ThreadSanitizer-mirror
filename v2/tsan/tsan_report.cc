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

void PrintReport(const ReportDesc *rep) {
  Printf("WARNING: Data race\n");
  for (int i = 0; i < rep->nmop; i++) {
    const ReportMop *mop = &rep->mop[i];
    Printf("  %s of size %d at %p by thread %d:\n",
           (mop->write ? "Write" : "Read"),
           mop->size, (void*)mop->addr, mop->tid);
    for (int j = 0; j < mop->stack.cnt; j++) {
      const ReportStackEntry *ent = &mop->stack.entry[j];
      Printf("    #%d %p: %s %s:%d\n", j,
             (void*)ent->pc, ent->func, ent->file, ent->line);
    }
  }
}

}  // namespace __tsan
