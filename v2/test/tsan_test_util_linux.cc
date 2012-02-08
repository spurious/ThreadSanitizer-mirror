//===-- tsan_test_util_linux.cc ---------------------------------*- C++ -*-===//
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
// Test utils, linux implementation.
//===----------------------------------------------------------------------===//

#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "tsan_atomic.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

#include <queue>

using __tsan::memory_order_relaxed;
using __tsan::memory_order_consume;
using __tsan::memory_order_acquire;
using __tsan::memory_order_release;
using __tsan::memory_order_acq_rel;
using __tsan::memory_order_seq_cst;
using __tsan::atomic_uintptr_t;
using __tsan::atomic_load;
using __tsan::atomic_store;
using __tsan::atomic_fetch_add;

// A lock which is not observed by the race detector.
class HiddenLock {
 public:
  HiddenLock() : lock_(0) { }
  ~HiddenLock() { assert(lock_ == 0); }
  void Lock() {
    while (__sync_val_compare_and_swap(&lock_, 0, 1) != 0)
      usleep(0);
    assert(lock_ == 1);
  }
  void Unlock() {
    assert(lock_ == 1);
    int res =__sync_val_compare_and_swap(&lock_, 1, 0);
    assert(res == 1);
    (void)res;
  }
 private:
  int lock_;
};

class ScopedHiddenLock {
 public:
  explicit ScopedHiddenLock(HiddenLock *lock) : lock_(lock) {
    lock_->Lock();
  }
  ~ScopedHiddenLock() { lock_->Unlock(); }
 private:
  HiddenLock *lock_;
};

MemLoc::MemLoc(int offset_from_aligned) {
  assert(offset_from_aligned >= 0 && offset_from_aligned < 16);
  static uintptr_t foo;
  static atomic_uintptr_t uniq = {(uintptr_t)&foo};  // Some real address.
  loc_  = (void*)(atomic_fetch_add(&uniq, 32, memory_order_relaxed)
      + offset_from_aligned);
  fprintf(stderr, "MemLoc: %p\n", loc_);
}

MemLoc::~MemLoc() { }

struct Event {
  enum Type {
    SHUTDOWN,
    READ,
    WRITE
  };
  Type type;
  void *ptr;
  int arg1, arg2;
};

struct ScopedThread::Impl {
  pthread_t thread;
  atomic_uintptr_t event;  // Event*

  static void *ScopedThreadCallback(void *arg);
  void send(Event *ev);
};

void *ScopedThread::Impl::ScopedThreadCallback(void *arg) {
  Impl *impl = (Impl*)arg;
  for (;;) {
    Event* ev = (Event*)atomic_load(&impl->event, memory_order_acquire);
    if (ev == NULL) {
      pthread_yield();
      continue;
    }
    if (ev->type == Event::SHUTDOWN) {
      atomic_store(&impl->event, 0, memory_order_release);
      break;
    }
    switch (ev->type) {
    case Event::READ:
      switch (ev->arg1 /*size*/) {
        case 1: __tsan_read1(ev->ptr); break;
        case 2: __tsan_read2(ev->ptr); break;
        case 4: __tsan_read4(ev->ptr); break;
        case 8: __tsan_read8(ev->ptr); break;
        case 16: __tsan_read16(ev->ptr); break;
      }
      break;
    case Event::WRITE:
      switch (ev->arg1 /*size*/) {
        case 1: __tsan_write1(ev->ptr); break;
        case 2: __tsan_write2(ev->ptr); break;
        case 4: __tsan_write4(ev->ptr); break;
        case 8: __tsan_write8(ev->ptr); break;
        case 16: __tsan_write16(ev->ptr); break;
      }
      break;
    default: assert(0);
    }
    atomic_store(&impl->event, 0, memory_order_release);
  }
  return NULL;
}

void ScopedThread::Impl::send(Event *e) {
  assert(atomic_load(&event, memory_order_relaxed) == 0);
  atomic_store(&event, (uintptr_t)e, memory_order_release);
  while (atomic_load(&event, memory_order_acquire) != 0)
    pthread_yield();
}

ScopedThread::ScopedThread() {
  impl_ = new Impl;
  atomic_store(&impl_->event, 0, memory_order_relaxed);
  pthread_create(&impl_->thread, NULL,
      ScopedThread::Impl::ScopedThreadCallback, impl_);
}

ScopedThread::~ScopedThread() {
  Event event;
  event.type = Event::SHUTDOWN;
  impl_->send(&event);
  pthread_join(impl_->thread, NULL);
  delete impl_;
}

void ScopedThread::Access(const MemLoc &ml, bool is_write,
                          int size, bool expect_race) {
  Event event;
  event.type = is_write ? Event::WRITE : Event::READ;
  event.ptr = ml.loc();
  event.arg1 = size;
  event.arg2 = (int)expect_race;
  impl_->send(&event);
}
