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
#include "tsan_report.h"

#include "gtest/gtest.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

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

static __thread const ReportDesc *g_report;

static void *BeforeInitThread(void *param) {
  (void)param;
  return 0;
}

void TestMutexBeforeInit() {
  // Mutexes must be usable before __tsan_init();
  pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mtx);
  pthread_mutex_unlock(&mtx);
  pthread_mutex_destroy(&mtx);
  pthread_t thr;
  pthread_create(&thr, 0, BeforeInitThread, 0);
  pthread_join(thr, 0);
}

namespace __tsan {
bool OnReport(const ReportDesc *rep, bool suppressed) {
  g_report = rep;
  return true;
}
}

static void* allocate_addr(int size, int offset_from_aligned = 0) {
  static uintptr_t foo;
  static atomic_uintptr_t uniq = {(uintptr_t)&foo};  // Some real address.
  const int kAlign = 16;
  CHECK(offset_from_aligned < kAlign);
  size = (size + 2 * kAlign) & ~(kAlign - 1);
  uintptr_t addr = atomic_fetch_add(&uniq, size, memory_order_relaxed);
  return (void*)(addr + offset_from_aligned);
}

MemLoc::MemLoc(int offset_from_aligned)
  : loc_(allocate_addr(16, offset_from_aligned)) {
}

MemLoc::~MemLoc() {
}

Mutex::Mutex(Type type)
  : alive_()
  , type_(type) {
}

Mutex::~Mutex() {
  CHECK(!alive_);
}

void Mutex::Init() {
  CHECK(!alive_);
  alive_ = true;
  if (type_ == Normal)
    CHECK_EQ(pthread_mutex_init((pthread_mutex_t*)mtx_, NULL), 0);
  else if (type_ == Spin)
    CHECK_EQ(pthread_spin_init((pthread_spinlock_t*)mtx_, 0), 0);
  else if (type_ == RW)
    CHECK_EQ(pthread_rwlock_init((pthread_rwlock_t*)mtx_, 0), 0);
  else
    CHECK(0);
}

void Mutex::StaticInit() {
  CHECK(!alive_);
  CHECK(type_ == Normal);
  alive_ = true;
  pthread_mutex_t tmp = PTHREAD_MUTEX_INITIALIZER;
  memcpy(mtx_, &tmp, sizeof(tmp));
}

void Mutex::Destroy() {
  CHECK(alive_);
  alive_ = false;
  if (type_ == Normal)
    CHECK_EQ(pthread_mutex_destroy((pthread_mutex_t*)mtx_), 0);
  else if (type_ == Spin)
    CHECK_EQ(pthread_spin_destroy((pthread_spinlock_t*)mtx_), 0);
  else if (type_ == RW)
    CHECK_EQ(pthread_rwlock_destroy((pthread_rwlock_t*)mtx_), 0);
}

void Mutex::Lock() {
  CHECK(alive_);
  if (type_ == Normal)
    CHECK_EQ(pthread_mutex_lock((pthread_mutex_t*)mtx_), 0);
  else if (type_ == Spin)
    CHECK_EQ(pthread_spin_lock((pthread_spinlock_t*)mtx_), 0);
  else if (type_ == RW)
    CHECK_EQ(pthread_rwlock_wrlock((pthread_rwlock_t*)mtx_), 0);
}

bool Mutex::TryLock() {
  CHECK(alive_);
  if (type_ == Normal)
    return pthread_mutex_trylock((pthread_mutex_t*)mtx_) == 0;
  else if (type_ == Spin)
    return pthread_spin_trylock((pthread_spinlock_t*)mtx_) == 0;
  else if (type_ == RW)
    return pthread_rwlock_trywrlock((pthread_rwlock_t*)mtx_) == 0;
  return false;
}

void Mutex::Unlock() {
  CHECK(alive_);
  if (type_ == Normal)
    CHECK_EQ(pthread_mutex_unlock((pthread_mutex_t*)mtx_), 0);
  else if (type_ == Spin)
    CHECK_EQ(pthread_spin_unlock((pthread_spinlock_t*)mtx_), 0);
  else if (type_ == RW)
    CHECK_EQ(pthread_rwlock_unlock((pthread_rwlock_t*)mtx_), 0);
}

void Mutex::ReadLock() {
  CHECK(alive_);
  CHECK(type_ == RW);
  CHECK_EQ(pthread_rwlock_rdlock((pthread_rwlock_t*)mtx_), 0);
}

bool Mutex::TryReadLock() {
  CHECK(alive_);
  CHECK(type_ == RW);
  return pthread_rwlock_tryrdlock((pthread_rwlock_t*)mtx_) ==  0;
}

void Mutex::ReadUnlock() {
  CHECK(alive_);
  CHECK(type_ == RW);
  CHECK_EQ(pthread_rwlock_unlock((pthread_rwlock_t*)mtx_), 0);
}

struct Event {
  enum Type {
    SHUTDOWN,
    READ,
    WRITE,
    CALL,
    RETURN,
    MUTEX_CREATE,
    MUTEX_DESTROY,
    MUTEX_LOCK,
    MUTEX_TRYLOCK,
    MUTEX_UNLOCK,
    MUTEX_READLOCK,
    MUTEX_TRYREADLOCK,
    MUTEX_READUNLOCK,
  };
  Type type;
  void *ptr;
  int arg;
  bool res;
  bool expect_report;
  __tsan::ReportType report_type;
  const ReportDesc *rep;

  Event(Type type, const void *ptr = NULL, int arg = 0)
    : type(type)
    , ptr(const_cast<void*>(ptr))
    , arg(arg)
    , res()
    , expect_report()
    , report_type()
    , rep() {
  }

  void ExpectReport(__tsan::ReportType type) {
    expect_report = true;
    report_type = type;
  }
};

struct ScopedThread::Impl {
  pthread_t thread;
  bool main;
  bool detached;
  atomic_uintptr_t event;  // Event*

  static void *ScopedThreadCallback(void *arg);
  void send(Event *ev);
  void HandleEvent(Event *ev);
};

void ScopedThread::Impl::HandleEvent(Event *ev) {
  CHECK_EQ(g_report, NULL);
  switch (ev->type) {
  case Event::READ:
  case Event::WRITE: {
    void (*tsan_mop)(void *addr) = NULL;
    if (ev->type == Event::READ) {
      switch (ev->arg /*size*/) {
        case 1: tsan_mop = __tsan_read1; break;
        case 2: tsan_mop = __tsan_read2; break;
        case 4: tsan_mop = __tsan_read4; break;
        case 8: tsan_mop = __tsan_read8; break;
        case 16: tsan_mop = __tsan_read16; break;
      }
    } else {
      switch (ev->arg /*size*/) {
        case 1: tsan_mop = __tsan_write1; break;
        case 2: tsan_mop = __tsan_write2; break;
        case 4: tsan_mop = __tsan_write4; break;
        case 8: tsan_mop = __tsan_write8; break;
        case 16: tsan_mop = __tsan_write16; break;
      }
    }
    CHECK_NE(tsan_mop, NULL);
    tsan_mop(ev->ptr);
    break;
  }
  case Event::CALL:
    __tsan_func_entry(ev->ptr);
    break;
  case Event::RETURN:
    __tsan_func_exit();
    break;
  case Event::MUTEX_CREATE:
    static_cast<Mutex*>(ev->ptr)->Init();
    break;
  case Event::MUTEX_DESTROY:
    static_cast<Mutex*>(ev->ptr)->Destroy();
    break;
  case Event::MUTEX_LOCK:
    static_cast<Mutex*>(ev->ptr)->Lock();
    break;
  case Event::MUTEX_TRYLOCK:
    ev->res = static_cast<Mutex*>(ev->ptr)->TryLock();
    break;
  case Event::MUTEX_UNLOCK:
    static_cast<Mutex*>(ev->ptr)->Unlock();
    break;
  case Event::MUTEX_READLOCK:
    static_cast<Mutex*>(ev->ptr)->ReadLock();
    break;
  case Event::MUTEX_TRYREADLOCK:
    ev->res = static_cast<Mutex*>(ev->ptr)->TryReadLock();
    break;
  case Event::MUTEX_READUNLOCK:
    static_cast<Mutex*>(ev->ptr)->ReadUnlock();
    break;
  default: CHECK(0);
  }
  ev->rep = g_report;
  g_report = NULL;
  if (ev->expect_report) {
    if (ev->rep == NULL) {
      printf("Missed expected report of type %d\n", (int)ev->report_type);
      EXPECT_FALSE("Missed expected race");
    }
    if (ev->rep->typ != ev->report_type) {
      printf("Expected report of type %d, got type %d\n",
             (int)ev->report_type, (int)ev->rep->typ);
      EXPECT_FALSE("Wrong report type");
    }
  } else {
    if (ev->rep) {
      __tsan::PrintReport(ev->rep);
      EXPECT_FALSE("Unexpected report");
    }
  }
}

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
    impl->HandleEvent(ev);
    atomic_store(&impl->event, 0, memory_order_release);
  }
  return NULL;
}

void ScopedThread::Impl::send(Event *e) {
  if (main) {
    HandleEvent(e);
  } else {
    CHECK_EQ(atomic_load(&event, memory_order_relaxed), 0);
    atomic_store(&event, (uintptr_t)e, memory_order_release);
    while (atomic_load(&event, memory_order_acquire) != 0)
      pthread_yield();
  }
}

ScopedThread::ScopedThread(bool detached, bool main) {
  impl_ = new Impl;
  impl_->main = main;
  impl_->detached = detached;
  atomic_store(&impl_->event, 0, memory_order_relaxed);
  if (!main) {
    pthread_create(&impl_->thread, NULL,
        ScopedThread::Impl::ScopedThreadCallback, impl_);
  }
}

ScopedThread::~ScopedThread() {
  if (!impl_->main) {
    Event event(Event::SHUTDOWN);
    impl_->send(&event);
    if (!impl_->detached)
      pthread_join(impl_->thread, NULL);
  }
  delete impl_;
}

void ScopedThread::Detach() {
  CHECK(!impl_->main);
  CHECK(!impl_->detached);
  impl_->detached = true;
  pthread_detach(impl_->thread);
}

const ReportDesc *ScopedThread::Access(void *addr, bool is_write,
                                       int size, bool expect_race) {
  (void)expect_race;
  Event event(is_write ? Event::WRITE : Event::READ, addr, size);
  if (expect_race)
    event.ExpectReport(__tsan::ReportTypeRace);
  impl_->send(&event);
  return event.rep;
}

void ScopedThread::Call(void(*pc)()) {
  Event event(Event::CALL, (void*)pc);
  impl_->send(&event);
}

void ScopedThread::Return() {
  Event event(Event::RETURN);
  impl_->send(&event);
}

void ScopedThread::Create(const Mutex &m) {
  Event event(Event::MUTEX_CREATE, &m);
  impl_->send(&event);
}

void ScopedThread::Destroy(const Mutex &m) {
  Event event(Event::MUTEX_DESTROY, &m);
  impl_->send(&event);
}

void ScopedThread::Lock(const Mutex &m) {
  Event event(Event::MUTEX_LOCK, &m);
  impl_->send(&event);
}

bool ScopedThread::TryLock(const Mutex &m) {
  Event event(Event::MUTEX_TRYLOCK, &m);
  impl_->send(&event);
  return event.res;
}

void ScopedThread::Unlock(const Mutex &m) {
  Event event(Event::MUTEX_UNLOCK, &m);
  impl_->send(&event);
}

void ScopedThread::ReadLock(const Mutex &m) {
  Event event(Event::MUTEX_READLOCK, &m);
  impl_->send(&event);
}

bool ScopedThread::TryReadLock(const Mutex &m) {
  Event event(Event::MUTEX_TRYREADLOCK, &m);
  impl_->send(&event);
  return event.res;
}

void ScopedThread::ReadUnlock(const Mutex &m) {
  Event event(Event::MUTEX_READUNLOCK, &m);
  impl_->send(&event);
}
