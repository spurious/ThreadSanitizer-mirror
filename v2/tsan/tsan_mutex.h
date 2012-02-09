//===-- tsan_mutex.h --------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_MUTEX_H
#define TSAN_MUTEX_H

#include "tsan_atomic.h"

namespace __tsan {

class Mutex {
 public:
  Mutex();
  ~Mutex();

  void Lock();
  void Unlock();

 private:
  atomic_uintptr_t state_;

  Mutex(const Mutex&);
  void operator = (const Mutex&);
};

class Lock {
 public:
  explicit Lock(Mutex *m);
  ~Lock();

 private:
  Mutex *m_;

  Lock(const Lock&);
  void operator = (const Lock&);
};

}  // namespace __tsan

#endif  // TSAN_MUTEX_H
