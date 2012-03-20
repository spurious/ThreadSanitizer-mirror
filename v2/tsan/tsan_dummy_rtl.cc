//===-- tsan_dummy_rtl.cc -------------------------------------------------===//
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
void __tsan_init() { }

void __tsan_read1(void *addr) { }
void __tsan_read2(void *addr) { }
void __tsan_read4(void *addr) { }
void __tsan_read8(void *addr) { }
void __tsan_read16(void *addr) { }

void __tsan_write1(void *addr) { }
void __tsan_write2(void *addr) { }
void __tsan_write4(void *addr) { }
void __tsan_write8(void *addr) { }
void __tsan_write16(void *addr) { }

void __tsan_vptr_update(void **vptr_p, void *new_val) { }

void __tsan_func_entry(void *call_pc) { }
void __tsan_func_exit() { }
