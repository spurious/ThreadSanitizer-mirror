//===-- tsan_interface.cc ---------------------------------------*- C++ -*-===//
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
#include "tsan_rtl.h"

#define CALLERPC ((uptr)__builtin_return_address(1))
using __tsan::uptr;
static __thread __tsan::ThreadState thr;

void __tsan_init() {
  __tsan::Initialize();
}

void __tsan_read1(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 1, false);
}

void __tsan_read2(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 2, false);
}

void __tsan_read4(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 4, false);
}

void __tsan_read8(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 8, false);
}

void __tsan_read16(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 8, false);
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr + 8, 8, false);
}

void __tsan_write1(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 1, true);
}

void __tsan_write2(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 1, true);
}

void __tsan_write4(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 1, true);
}

void __tsan_write8(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 1, true);
}

void __tsan_write16(void *addr) {
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr, 8, true);
  __tsan::MemoryAccess(&thr, CALLERPC, (uptr)addr + 8, 8, true);
}

void __tsan_func_entry(void *pc) {
  __tsan::FuncEntry(&thr, (uptr)pc);
}

void __tsan_func_exit() {
  __tsan::FuncExit(&thr);
}

int  __tsan_thread_create() {
  return __tsan::ThreadCreate(&thr);
}

void __tsan_thread_start(int tid) {
  __tsan::ThreadStart(&thr, tid);
}

void __tsan_mutex_create(void *addr, int is_rw) {
  __tsan::MutexCreate(&thr, (uptr)addr, !!is_rw);
}

void __tsan_mutex_destroy(void *addr) {
  __tsan::MutexDestroy(&thr, (uptr)addr);
}

void __tsan_mutex_lock(void *addr) {
  __tsan::MutexLock(&thr, (uptr)addr);
}

void __tsan_mutex_unlock(void *addr) {
  __tsan::MutexUnlock(&thr, (uptr)addr);
}
