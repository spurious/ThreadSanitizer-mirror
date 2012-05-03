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
#include "tsan_platform.h"
#include "tsan_rtl.h"

namespace __tsan {

static void PrintStack(const ReportStack *ent) {
  for (int i = 0; ent; ent = ent->next, i++) {
    Printf("    #%d %s %s:%d", i, ent->func, ent->file, ent->line);
    if (ent->col)
      Printf(":%d", ent->col);
    if (ent->module && ent->offset)
      Printf(" (%s+%p)\n", ent->module, (void*)ent->offset);
    else
      Printf(" (%p)\n", (void*)ent->pc);
  }
}

void PrintReport(const ReportDesc *rep) {
  Printf("==================\n");
  Printf("WARNING: ThreadSanitizer: ");
  if (rep->typ == ReportTypeRace)
    Printf("data race");
  else if (rep->typ == ReportTypeThreadLeak)
    Printf("thread leak");
  else if (rep->typ == ReportTypeMutexDestroyLocked)
    Printf("destroy of a locked mutex");
  else if (rep->typ == ReportTypeSignalUnsafe)
    Printf("signal-unsafe call inside of a signal");

  Printf(" (pid=%d)\n", GetPid());

  if (rep->stack)
    PrintStack(rep->stack);

  for (int i = 0; i < rep->nmop; i++) {
    const ReportMop *mop = &rep->mop[i];
    Printf("  %s of size %d at %p",
        (i == 0 ? (mop->write ? "Write" : "Read")
                : (mop->write ? "Previous write" : "Previous read")),
        mop->size, (void*)mop->addr);
    if (mop->tid == 0)
      Printf(" by main thread:\n");
    else
      Printf(" by thread %d:\n", mop->tid);
    PrintStack(mop->stack);
  }

  if (rep->loc) {
    const ReportLocation *loc = rep->loc;
    if (loc->type == ReportLocationGlobal) {
      Printf("  Location is global '%s' of size %lu at %lx %s:%d\n",
             loc->name, loc->size, loc->addr, loc->file, loc->line);
    } else if (loc->type == ReportLocationHeap) {
      Printf("  Location is heap of size %lu at %lx allocated by thread %d:\n",
             loc->size, loc->addr, loc->tid);
      PrintStack(loc->stack);
    } else if (loc->type == ReportLocationStack) {
      Printf("  Location is stack of thread %d:\n", loc->tid);
    }
  }

  for (int i = 0; i < rep->nmutex; i++) {
    ReportMutex *rm = &rep->mutex[i];
    if (rm->stack == 0)
      continue;
    Printf("  Mutex %d created at:\n", rm->id);
    PrintStack(rm->stack);
  }

  for (int i = 0; i < rep->nthread; i++) {
    ReportThread *rt = &rep->thread[i];
    if (rt->id == 0)  // Little sense in describing the main thread.
      continue;
    Printf("  Thread %d", rt->id);
    if (rt->name)
      Printf(" '%s'", rt->name);
    Printf(" (%s)", rt->running ? "running" : "finished");
    if (rt->stack)
      Printf(" created at:");
    Printf("\n");
    PrintStack(rt->stack);
  }
  Printf("==================\n");
}

RegionAlloc::RegionAlloc(void *mem, uptr size)
  : mem_((char*)mem)
  , end_((char*)mem + size) {
  CHECK_NE(mem, 0);
  CHECK_GT(size, 0);
}

void *RegionAlloc::Alloc(uptr size) {
  char *p = mem_;
  if (p + size > end_)
    return 0;
  mem_ += size;
  return p;
}

char *RegionAlloc::Strdup(const char *str) {
  if (str == 0)
    return 0;
  uptr len = internal_strlen(str) + 1;
  char *p = (char*)Alloc(len);
  if (p == 0)
    return 0;
  internal_memcpy(p, str, len);
  return p;
}

}  // namespace __tsan
