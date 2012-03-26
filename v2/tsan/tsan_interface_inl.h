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
#include "tsan_rtl.h"

#define CALLERPC ((uptr)__builtin_return_address(0))
using __tsan::uptr;
using __tsan::cur_thread;

void FLATTEN __tsan_read1(void *addr) {
  __tsan::MemoryAccess<0, 0>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_read2(void *addr) {
  __tsan::MemoryAccess<1, 0>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_read4(void *addr) {
  __tsan::MemoryAccess<2, 0>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_read8(void *addr) {
  __tsan::MemoryAccess<3, 0>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_write1(void *addr) {
  __tsan::MemoryAccess<0, 1>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_write2(void *addr) {
  __tsan::MemoryAccess<1, 1>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_write4(void *addr) {
  __tsan::MemoryAccess<2, 1>(cur_thread(), CALLERPC, (uptr)addr);
}

void FLATTEN __tsan_write8(void *addr) {
  __tsan::MemoryAccess<3, 1>(cur_thread(), CALLERPC, (uptr)addr);
}

void __tsan_read16(void *addr) {
  __tsan::MemoryAccess<3, 0>(cur_thread(), CALLERPC, (uptr)addr);
  __tsan::MemoryAccess<3, 0>(cur_thread(), CALLERPC, (uptr)addr + 8);
}

void __tsan_write16(void *addr) {
  __tsan::MemoryAccess<3, 0>(cur_thread(), CALLERPC, (uptr)addr);
  __tsan::MemoryAccess<3, 0>(cur_thread(), CALLERPC, (uptr)addr + 8);
}

void FLATTEN __tsan_vptr_update(void **vptr_p, void *new_val) {
  CHECK_EQ(sizeof(vptr_p), 8);
  if (*vptr_p != new_val)
    __tsan::MemoryAccess<3, 1>(cur_thread(), CALLERPC, (uptr)vptr_p);
}

void FLATTEN __tsan_func_entry(void *pc) {
  __tsan::FuncEntry(cur_thread(), (uptr)pc);
}

void FLATTEN __tsan_func_exit() {
  __tsan::FuncExit(cur_thread());
}
