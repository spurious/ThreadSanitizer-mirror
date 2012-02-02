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

#include "tsan_test_util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

#include <queue>

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
  static uintptr_t uniq = 0x20000;  // something far enough from 0 and 8-aligned
  loc_  = (void*)(__sync_fetch_and_add(&uniq, 8) + offset_from_aligned);
  fprintf(stderr, "MemLoc: %p\n", loc_);
}

MemLoc::~MemLoc() { }

struct Event {
  ScopedThread *t;
  enum Type {
    READ,
    WRITE
  };
  Type type;
  void *ptr;
  int arg1, arg2;
};


static HiddenLock event_queue_lock;
static std::queue<Event> event_queue;

struct ScopedThread::Impl {
  pthread_t thread;
  ScopedThread *t;

  bool want_to_exit;

  static void *ScopedThreadCallback(void *arg);
};

void *ScopedThread::Impl::ScopedThreadCallback(void *arg) {
  Impl *impl = (Impl*)arg;
  ScopedThread *t = impl->t;
  fprintf(stderr, "ScopedThreadCallback: %p\n", t);
  while (true) {
    Event event;
    bool queue_is_empty;
    {
      ScopedHiddenLock lock(&event_queue_lock);
      if (event_queue.empty()) {
        if (impl->want_to_exit) break;
        continue;
      }
      event = event_queue.front();
      if (event.t != impl->t) continue;
      event_queue.pop();
    }
    fprintf(stderr, "T%p Event: %d %p %d %d\n", t,
        event.type, event.ptr,
        event.arg1, event.arg2);
  }
  return NULL;
}

ScopedThread::ScopedThread() {
  impl_ = new Impl;
  impl_->t = this;
  impl_->want_to_exit = false;
  pthread_create(&impl_->thread, NULL,
      ScopedThread::Impl::ScopedThreadCallback, impl_);
}

ScopedThread::~ScopedThread() {
  {
    ScopedHiddenLock lock(&event_queue_lock);
    impl_->want_to_exit = true;
  }
  pthread_join(impl_->thread, NULL);
  delete impl_;
}

void ScopedThread::Access(const MemLoc &ml, bool is_write,
                          int size, bool expect_race) {
  Event event;
  event.t = this;
  event.type = is_write ? Event::WRITE : Event::READ;
  event.ptr = ml.loc();
  event.arg1 = size;
  event.arg2 = (int)expect_race;
  ScopedHiddenLock lock(&event_queue_lock);
  event_queue.push(event);
}
