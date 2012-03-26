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
#include "tsan_interface_ann.h"
#include "tsan_rtl.h"

#define CALLERPC ((uptr)__builtin_return_address(0))
using __tsan::uptr;
using __tsan::cur_thread;

void __tsan_init() {
  __tsan::Initialize(cur_thread());
}

void __tsan_acquire(void *addr) {
  __tsan::Acquire(cur_thread(), CALLERPC, (uptr)addr);
}

void __tsan_release(void *addr) {
  __tsan::Release(cur_thread(), CALLERPC, (uptr)addr);
}
