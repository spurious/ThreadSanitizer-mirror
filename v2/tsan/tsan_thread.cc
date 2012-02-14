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

Thread *Thread::Create(void *(*callback)(void *param), void *param,
                       uptr uid, bool detached) {
  Thread *t = new Thread;  // FIXME: we probably don't want 'new'.
  t->callback_ = callback;
  t->param_ = param;
  t->tid_ = ThreadCreate(&cur_thread, uid, detached);
  return t;
}

void *Thread::Start() {
  // Printf("ThreadStart: %p\n", this);
  ThreadStart(&cur_thread, tid_);
  void *res = callback_(param_);
  ThreadFinish(&cur_thread);
  return res;
}

}  // namespace __tsan
