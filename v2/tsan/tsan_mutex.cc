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

namespace __tsan {

Mutex::Mutex() {
  atomic_store(&state_, 0, memory_order_relaxed);
}

Mutex::~Mutex() {
  CHECK_EQ(atomic_load(&state_, memory_order_relaxed), 0);
}

void Mutex::Lock() {
  while (atomic_exchange(&state_, 1, memory_order_acquire) == 0) {
  }
}

void Mutex::Unlock() {
  atomic_store(&state_, 0, memory_order_release);
}

Lock::Lock(Mutex *m)
  : m_(m) {
  m_->Lock();
}

Lock::~Lock() {
  m_->Unlock();
}

}  // namespace __tsan
