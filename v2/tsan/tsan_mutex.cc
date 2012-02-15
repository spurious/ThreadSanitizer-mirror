//===-- tsan_mutex.cc -------------------------------------------*- C++ -*-===//
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
#include "tsan_mutex.h"
#include "tsan_platform.h"

namespace __tsan {

Mutex::Mutex() {
  atomic_store(&state_, 0, memory_order_relaxed);
}

Mutex::~Mutex() {
  CHECK_EQ(atomic_load(&state_, memory_order_relaxed), 0);
}

void Mutex::Lock() {
  if (atomic_exchange(&state_, 1, memory_order_acquire) == 0)
    return;
  for (int i = 0;; i++) {
    if (i < 10)
      proc_yield(20);
    else
      sched_yield();
    if (atomic_load(&state_, memory_order_relaxed) == 0)
      if (atomic_exchange(&state_, 1, memory_order_acquire) == 0)
        return;
  }
}

void Mutex::Unlock() {
  atomic_store(&state_, 0, memory_order_release);
}

void Mutex::ReadLock() {
  Lock();
}

void Mutex::ReadUnlock() {
  Unlock();
}

Lock::Lock(Mutex *m)
  : m_(m) {
  m_->Lock();
}

Lock::~Lock() {
  m_->Unlock();
}

ReadLock::ReadLock(Mutex *m)
  : m_(m) {
  m_->ReadLock();
}

ReadLock::~ReadLock() {
  m_->ReadUnlock();
}

}  // namespace __tsan
