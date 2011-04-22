/* Copyright (c) 2010-2011, Google Inc.
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

// Author: glider@google.com (Alexander Potapenko)

#define DYNAMIC_ANNOTATIONS_PREFIX LLVM
#include "dynamic_annotations.h"
#include "fake_annotations.h"
#include "tsan_rtl.h"
#include "tsan_rtl_sched_shake.h"

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
void DYNAMIC_ANNOTATIONS_NAME(AnnotateHappensBefore)(const char *file, int line,
                                                     const volatile void *cv) {
  tsan_rtl_sched_shake(shake_atomic_rmw, cv);
  DECLARE_TID_AND_PC();
  ExSPut(SIGNAL, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateHappensAfter)(const char *file, int line,
                                                    const volatile void *cv) {
  //TODO(dvyukov): shake must be placed *before* the user's *action*
  tsan_rtl_sched_shake(shake_atomic_rmw, cv);
  DECLARE_TID_AND_PC();
  ExSPut(WAIT, tid, pc, (uintptr_t)cv, 0);
}

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
void DYNAMIC_ANNOTATIONS_NAME(AnnotateRWLockCreate)(const char *file, int line,
                                                    const volatile void *lock) {
  DECLARE_TID_AND_PC();
  ExSPut(LOCK_CREATE, tid, pc, (uintptr_t)lock, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateRWLockDestroy)(const char *file, int line,
                                                     const volatile void *lock) {
  DECLARE_TID_AND_PC();
  ExSPut(LOCK_DESTROY, tid, pc, (uintptr_t)lock, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateRWLockAcquired)(const char *file, int line,
                                                      const volatile void *lock,
                                                      long int is_w) {
  DECLARE_TID_AND_PC();
  ExSPut(is_w ? WRITER_LOCK : READER_LOCK, tid, pc, (uintptr_t)lock, 0);
}

extern "C"
void DYNAMIC_ANNOTATIONS_NAME(AnnotateRWLockReleased)(
    const char *file, int line, const volatile void *lock, long int is_w) {
  DECLARE_TID_AND_PC();
  ExSPut(UNLOCK, tid, pc, (uintptr_t)lock, 0);
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

// WebKit WTF annotations {{{1
extern "C"
void WTFAnnotateHappensBefore(const char *file, int line,
                              const volatile void *cv) {
  tsan_rtl_sched_shake(shake_atomic_rmw, cv);
  DECLARE_TID_AND_PC();
  ExSPut(SIGNAL, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void WTFAnnotateHappensAfter(const char *file, int line,
                             const volatile void *cv) {
  //TODO(dvyukov): shake must be placed *before* the user's *action*
  tsan_rtl_sched_shake(shake_atomic_rmw, cv);
  DECLARE_TID_AND_PC();
  ExSPut(WAIT, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void WTFAnnotateBenignRaceSized(
    const char *file, int line,
    const volatile void *mem, long size, const char *description) {
  DECLARE_TID();
  ExSPut(BENIGN_RACE, tid, (uintptr_t)description,
      (uintptr_t)mem, (uintptr_t)size);
}

// }}}
// Fake annotations (not from dynamic_annotations.h) {{{1
extern "C"
void FakeAnnotatePrintStackTrace(const char *file, int line) {
  PrintStackTrace();
}
// }}}

// TODO(glider): we may need a flag to tune this.
extern "C"
int RunningOnValgrind(void) {
  return 0;
}

// }}}

#undef DYNAMIC_ANNOTATIONS_PREFIX
