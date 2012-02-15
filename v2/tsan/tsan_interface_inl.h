//===-- tsan_interface_inl.h ------------------------------------*- C++ -*-===//
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
#include "tsan_interface_ann.h"
#include "tsan_rtl.h"

#define CALLERPC ((uptr)__builtin_return_address(0))
using __tsan::uptr;
using __tsan::cur_thread;

void __tsan_init() {
  __tsan::Initialize(cur_thread());
}

void FLATTEN __tsan_read1(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 1, false);
}

void FLATTEN __tsan_read2(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 2, false);
}

void FLATTEN __tsan_read4(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 4, false);
}

void FLATTEN __tsan_read8(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 8, false);
}

void FLATTEN __tsan_read16(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 8, false);
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr + 8, 8, false);
}

void FLATTEN __tsan_write1(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 1, true);
}

void FLATTEN __tsan_write2(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 2, true);
}

void FLATTEN __tsan_write4(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 4, true);
}

void FLATTEN __tsan_write8(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 8, true);
}

void FLATTEN __tsan_write16(void *addr) {
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr, 8, true);
  __tsan::MemoryAccess(cur_thread(), CALLERPC, (uptr)addr + 8, 8, true);
}

void FLATTEN __tsan_func_entry(void *pc) {
  __tsan::FuncEntry(cur_thread(), (uptr)pc);
}

void FLATTEN __tsan_func_exit() {
  __tsan::FuncExit(cur_thread());
}

void __tsan_acquire(void *addr) {
  __tsan::Acquire(cur_thread(), CALLERPC, (uptr)addr);
}

void __tsan_release(void *addr) {
  __tsan::Release(cur_thread(), CALLERPC, (uptr)addr);
}
