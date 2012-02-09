//===-- tsan_thread.cc ------------------------------------------*- C++ -*-===//
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
// Thread.
//===----------------------------------------------------------------------===//

#include "tsan_thread.h"

#include "tsan_rtl.h"

namespace __tsan {

Thread *Thread::Create(void *callback, void *param) {
  Thread *t = new Thread;  // FIXME: we probably don't want 'new'.
  t->callback_ = callback;
  t->param_ = param;
  return t;
}

void *Thread::ThreadStart() {
  Printf("ThreadStart: %p\n", this);
  typedef void *(*callback_t)(void *param);
  callback_t c = (callback_t)callback_;
  return c(param_);
}

}  // namespace __tsan
