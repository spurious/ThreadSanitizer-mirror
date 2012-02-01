//===-- tsan_rtl.cc ---------------------------------------------*- C++ -*-===//
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
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "tsan_interface.h"
#include "tsan_rtl.h"

namespace __tsan {

static void Initialize() {
  Printf("tsan::Initialize\n");
}

ALWAYS_INLINE
static void OnMemoryAccess(void *addr, int size, bool is_write) {
  Printf("tsan::OnMemoryAccess: %p size=%d is_write=%d\n", addr, size, is_write);
}

ALWAYS_INLINE
static void FuncEntry(void *call_pc) {
  Printf("tsan::FuncEntry %p\n", call_pc);
}

ALWAYS_INLINE
static void FuncExit() {
  Printf("tsan::FuncExit\n");
}

}  // namespace __tsan

//===------------- Interface functions    --------------------------------{{{1
void __tsan_init() { __tsan::Initialize(); }

void __tsan_read1(void *addr) { __tsan::OnMemoryAccess(addr, 1, false); }
void __tsan_read2(void *addr) { __tsan::OnMemoryAccess(addr, 2, false); }
void __tsan_read4(void *addr) { __tsan::OnMemoryAccess(addr, 4, false); }
void __tsan_read8(void *addr) { __tsan::OnMemoryAccess(addr, 8, false); }
void __tsan_read16(void *addr) {
  __tsan::OnMemoryAccess(addr, 8, false);
  __tsan::OnMemoryAccess((char*)addr + 8, 8, false);
}

void __tsan_write1(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write2(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write4(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write8(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write16(void *addr) {
  __tsan::OnMemoryAccess(addr, 8, true);
  __tsan::OnMemoryAccess((char*)addr + 8, 8, true);
}

void __tsan_func_entry(void *call_pc) { __tsan::FuncEntry(call_pc); }
void __tsan_func_exit() { __tsan::FuncExit(); }
