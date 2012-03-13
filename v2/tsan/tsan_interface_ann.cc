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
#include "tsan_rtl.h"

using namespace __tsan;  // NOLINT

extern "C" {
void AnnotateHappensBefore(char *f, int l, uptr *addr) {
}

void AnnotateHappensAfter(char *f, int l, uptr cv) {
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

void AnnotateFlushExpectedRaces(char *f, int l) {
}

void AnnotateEnableRaceDetection(char *f, int l, int enable) {
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
}

void AnnotateBenignRace(char *f, int l, uptr mem, char *desc) {
}

void AnnotateBenignRaceSized(char *f, int l, uptr mem, uptr size, char *desc) {
}

void AnnotateIgnoreReadsBegin(char *f, int l) {
}

void AnnotateIgnoreReadsEnd(char *f, int l) {
}

void AnnotateIgnoreWritesBegin(char *f, int l) {
}

void AnnotateIgnoreWritesEnd(char *f, int l) {
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
}  // extern "C"
