// Copyright 2010 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#define DYNAMIC_ANNOTATIONS_PREFIX LLVM
#include "dynamic_annotations.h"
#include "tsan_rtl.h"

#undef DECLARE_TID_AND_PC
#define DECLARE_TID_AND_PC() \
  tid_t tid = ExGetTid(); \
  pc_t pc = ExGetPc();

#undef DECLARE_TID
#define DECLARE_TID() \
  tid_t tid = ExGetTid();

extern bool global_ignore;

// dynamic_annotations {{{1
extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateCondVarSignal)(const char *file, int line,
                                                     const volatile void *cv) {
  DECLARE_TID_AND_PC();
  ExSPut(SIGNAL, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateMutexIsNotPHB)(const char *file, int line,
                                                     const volatile void *mu) {
  DECLARE_TID_AND_PC();
  ExSPut(NON_HB_LOCK, tid, pc, (uintptr_t)mu, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateCondVarWait)(const char *file, int line,
                                                   const volatile void *cv,
                                                   const volatile void *lock) {
  DECLARE_TID_AND_PC();
  ExSPut(WAIT, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateTraceMemory)(const char *file, int line,
                                                   const volatile void *mem) {
  DECLARE_TID_AND_PC();
  ExSPut(TRACE_MEM, tid, pc, (uintptr_t)mem, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateFlushState)(const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExSPut(FLUSH_STATE, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateNewMemory)(const char *file, int line,
                                                 const volatile void *mem,
                                                 long size) {
  DECLARE_TID_AND_PC();
  ExSPut(MALLOC, tid, pc, (uintptr_t)mem, size);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateNoOp)(
    const char *file, int line, const volatile void *mem) {
  // Do nothing.
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateFlushExpectedRaces)(
    const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExSPut(FLUSH_EXPECTED_RACES, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateEnableRaceDetection)(
    const char *file, int line, int enable) {
  set_global_ignore(!enable);
  fprintf(stderr, "enable: %d, global_ignore: %d\n", enable, global_ignore);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateMutexIsUsedAsCondVar)(
    const char *file, int line, const volatile void *mu) {
  DECLARE_TID_AND_PC();
  ExSPut(HB_LOCK, tid, pc, (uintptr_t)mu, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotatePCQGet)(
    const char *file, int line, const volatile void *pcq) {
  DECLARE_TID_AND_PC();
  ExSPut(PCQ_GET, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotatePCQPut)(
    const char *file, int line, const volatile void *pcq) {
  DECLARE_TID_AND_PC();
  ExSPut(PCQ_PUT, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotatePCQDestroy)(
    const char *file, int line, const volatile void *pcq) {
  DECLARE_TID_AND_PC();
  ExSPut(PCQ_DESTROY, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotatePCQCreate)(
    const char *file, int line, const volatile void *pcq) {
  DECLARE_TID_AND_PC();
  ExSPut(PCQ_CREATE, tid, pc, (uintptr_t)pcq, 0);
}


extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateExpectRace)(
    const char *file, int line,
    const volatile void *mem, const char *description) {
  tid_t tid = ExGetTid();
  ExSPut(EXPECT_RACE, tid, (uintptr_t)description, (uintptr_t)mem, 1);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateBenignRace)(
    const char *file, int line,
    const volatile void *mem, const char *description) {
  DECLARE_TID();
  ExSPut(BENIGN_RACE, tid, (uintptr_t)description, (uintptr_t)mem, 1);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateBenignRaceSized)(
    const char *file, int line,
    const volatile void *mem, long size, const char *description) {
  DECLARE_TID();
  ExSPut(BENIGN_RACE, tid, (uintptr_t)description,
      (uintptr_t)mem, (uintptr_t)size);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateIgnoreReadsBegin)(
    const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExPut(IGNORE_READS_BEG, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateIgnoreReadsEnd)(
    const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExPut(IGNORE_READS_END, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateIgnoreWritesBegin)(
    const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExPut(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateIgnoreWritesEnd)(
    const char *file, int line) {
  DECLARE_TID_AND_PC();
  ExPut(IGNORE_WRITES_END, tid, pc, 0, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotatePublishMemoryRange)(
    const char *file, int line, const volatile void *address, long size) {
  DECLARE_TID_AND_PC();
  ExPut(PUBLISH_RANGE, tid, pc, (uintptr_t)address, (uintptr_t)size);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateUnpublishMemoryRange)(
    const char *file, int line,
                                const volatile void *address, long size) {
  DECLARE_TID_AND_PC();
  ExPut(UNPUBLISH_RANGE, tid, pc, (uintptr_t)address, (uintptr_t)size);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateThreadName)(
    const char *file, int line, const char *thread_name) {
  DECLARE_TID_AND_PC();
  ExPut(SET_THREAD_NAME, tid, pc, (uintptr_t)thread_name, 0);
}

// TODO(glider): we may need a flag to tune this.
extern "C"
int RunningOnValgrind(void) {
  return 0;
}

// }}}

#undef DYNAMIC_ANNOTATIONS_PREFIX
