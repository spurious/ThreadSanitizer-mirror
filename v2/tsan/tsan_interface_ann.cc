//===-- tsan_interface_ann.cc -----------------------------------*- C++ -*-===//
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
#include "tsan_interface_ann.h"
#include "tsan_mutex.h"
#include "tsan_placement_new.h"
#include "tsan_report.h"
#include "tsan_rtl.h"
#include "tsan_slab.h"

#define CALLERPC ((uptr)__builtin_return_address(0))

using namespace __tsan;  // NOLINT

namespace __tsan {

static const int kMaxDescLen = 128;

struct ExpectRace {
  ExpectRace *next;
  ExpectRace *prev;
  int hitcount;
  uptr addr;
  uptr size;
  char *file;
  int line;
  char desc[kMaxDescLen];
};

struct DynamicAnnContext {
  Mutex mtx;
  SlabAlloc expect_alloc;
  SlabCache expect_slab;
  ExpectRace expect;
  ExpectRace benign;

  DynamicAnnContext()
    : mtx(StatMtxAnnotations)
    , expect_alloc(sizeof(ExpectRace))
    , expect_slab(&expect_alloc) {
  }
};

static DynamicAnnContext *dyn_ann_ctx;
static char dyn_ann_ctx_placeholder[sizeof(DynamicAnnContext)] ALIGN(64);

static void AddExpectRace(SlabCache *alloc, ExpectRace *list,
    char *f, int l, uptr mem, uptr size, char *desc) {
  ExpectRace *race = (ExpectRace*)alloc->Alloc();
  race->hitcount = 0;
  race->addr = mem;
  race->size = size;
  race->file = f;
  race->line = l;
  race->desc[0] = 0;
  if (desc) {
    int i = 0;
    for (; i < kMaxDescLen - 1 && desc[i]; i++)
      race->desc[i] = desc[i];
    race->desc[i] = 0;
  }
  race->prev = list;
  race->next = list->next;
  race->next->prev = race;
  list->next = race;
}

static bool CheckContains(ExpectRace *list, uptr addr, uptr size) {
  for (ExpectRace *race = list->next; race != list; race = race->next) {
    uptr maxbegin = max(race->addr, addr);
    uptr minend = min(race->addr + race->size, addr + size);
    if (maxbegin < minend) {
      DPrintf("Hit expected/benign race: %s addr=%p:%d %s:%d\n",
          race->desc, race->addr, (int)race->size, race->file, race->line);
      race->hitcount++;
      return true;
    }
  }
  return false;
}

static void InitList(ExpectRace *list) {
  list->next = list;
  list->prev = list;
}

void InitializeDynamicAnnotations() {
  dyn_ann_ctx = new(dyn_ann_ctx_placeholder) DynamicAnnContext;
  InitList(&dyn_ann_ctx->expect);
  InitList(&dyn_ann_ctx->benign);
}

bool IsExpectReport(uptr addr, uptr size) {
  Lock lock(&dyn_ann_ctx->mtx);
  if (CheckContains(&dyn_ann_ctx->expect, addr, size))
    return true;
  if (CheckContains(&dyn_ann_ctx->benign, addr, size))
    return true;
  return false;
}

}  // namespace __tsan

using namespace __tsan;  // NOLINT

extern "C" {
void AnnotateHappensBefore(char *f, int l, uptr addr) {
  Release(cur_thread(), CALLERPC, addr);
}

void AnnotateHappensAfter(char *f, int l, uptr addr) {
  Acquire(cur_thread(), CALLERPC, addr);
}

void AnnotateCondVarSignal(char *f, int l, uptr cv) {
}

void AnnotateCondVarSignalAll(char *f, int l, uptr cv) {
}

void AnnotateMutexIsNotPHB(char *f, int l, uptr mu) {
}

void AnnotateCondVarWait(char *f, int l, uptr cv, uptr lock) {
}

void AnnotateRWLockCreate(char *f, int l, uptr lock) {
}

void AnnotateRWLockDestroy(char *f, int l, uptr lock) {
}

void AnnotateRWLockAcquired(char *f, int l, uptr lock, uptr is_w) {
}

void AnnotateRWLockReleased(char *f, int l, uptr lock, uptr is_w) {
}

void AnnotateTraceMemory(char *f, int l, uptr mem) {
}

void AnnotateFlushState(char *f, int l) {
}

void AnnotateNewMemory(char *f, int l, uptr mem, uptr size) {
}

void AnnotateNoOp(char *f, int l, uptr mem) {
}

static void ReportMissedExpectedRace(ExpectRace *race) {
  Printf("==================\n");
  Printf("WARNING: ThreadSanitizer: missed expected data race\n");
  Printf("  %s addr=%p %s:%d\n",
      race->desc, race->addr, race->file, race->line);
  Printf("==================\n");
}

void AnnotateFlushExpectedRaces(char *f, int l) {
  Lock lock(&dyn_ann_ctx->mtx);
  while (dyn_ann_ctx->expect.next != &dyn_ann_ctx->expect) {
    ExpectRace *race = dyn_ann_ctx->expect.next;
    if (race->hitcount == 0)
      ReportMissedExpectedRace(race);
    race->prev->next = race->next;
    race->next->prev = race->prev;
    dyn_ann_ctx->expect_slab.Free(race);
  }
}

void AnnotateEnableRaceDetection(char *f, int l, int enable) {
  // FIXME: Reconsider this functionality later. It may be irrelevant.
}

void AnnotateMutexIsUsedAsCondVar(char *f, int l, uptr mu) {
}

void AnnotatePCQGet(char *f, int l, uptr pcq) {
}

void AnnotatePCQPut(char *f, int l, uptr pcq) {
}

void AnnotatePCQDestroy(char *f, int l, uptr pcq) {
}

void AnnotatePCQCreate(char *f, int l, uptr pcq) {
}

void AnnotateExpectRace(char *f, int l, uptr mem, char *desc) {
  Lock lock(&dyn_ann_ctx->mtx);
  AddExpectRace(&dyn_ann_ctx->expect_slab, &dyn_ann_ctx->expect,
                f, l, mem, 1, desc);
  DPrintf("Add expected race: %s addr=%p %s:%d\n", desc, mem, f, l);
}

// FIXME: Turn it off later. WTF is benign race?1?? Go talk to Hans Boehm.
void AnnotateBenignRaceSized(char *f, int l, uptr mem, uptr size, char *desc) {
  Lock lock(&dyn_ann_ctx->mtx);
  AddExpectRace(&dyn_ann_ctx->expect_slab, &dyn_ann_ctx->benign,
                f, l, mem, size, desc);
  DPrintf("Add benign race: %s addr=%p %s:%d\n", desc, mem, f, l);
}

void AnnotateBenignRace(char *f, int l, uptr mem, char *desc) {
  AnnotateBenignRaceSized(f, l, mem, 1, desc);
}

void AnnotateIgnoreReadsBegin(char *f, int l) {
  IgnoreCtl(cur_thread(), false, true);
}

void AnnotateIgnoreReadsEnd(char *f, int l) {
  IgnoreCtl(cur_thread(), false, false);
}

void AnnotateIgnoreWritesBegin(char *f, int l) {
  IgnoreCtl(cur_thread(), true, true);
}

void AnnotateIgnoreWritesEnd(char *f, int l) {
  IgnoreCtl(cur_thread(), true, false);
}

void AnnotatePublishMemoryRange(char *f, int l, uptr addr, uptr size) {
}

void AnnotateUnpublishMemoryRange(char *f, int l, uptr addr, uptr size) {
}

void AnnotateThreadName(char *f, int l, char *name) {
}

void WTFAnnotateHappensBefore(char *f, int l, uptr addr) {
}

void WTFAnnotateHappensAfter(char *f, int l, uptr addr) {
}

void WTFAnnotateBenignRaceSized(char *f, int l, uptr mem, uptr sz, char *desc) {
}

int RunningOnValgrind() {
  return 1;
}

const char *ThreadSanitizerQuery(const char *query) {
  if (internal_strcmp(query, "pure_happens_before") == 0)
    return "1";
  else
    return "0";
}
}  // extern "C"
