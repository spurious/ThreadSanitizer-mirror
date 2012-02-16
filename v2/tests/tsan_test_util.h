//===-- tsan_test_util.h ----------------------------------------*- C++ -*-===//
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
// Test utils.
//===----------------------------------------------------------------------===//
#ifndef TSAN_TEST_UTIL_H
#define TSAN_TEST_UTIL_H

namespace __tsan {
struct ReportDesc;
};
using __tsan::ReportDesc;

void TestMutexBeforeInit();

// A location of memory on which a race may be detected.
class MemLoc {
 public:
  explicit MemLoc(int offset_from_aligned = 0);
  ~MemLoc();
  void *loc() const { return loc_; }
 private:
  void *const loc_;
  MemLoc(const MemLoc&);
  void operator = (const MemLoc&);
};

class Mutex {
 public:
  enum Type { Normal, Spin, RW };

  explicit Mutex(Type type = Normal);
  ~Mutex();

  void Init();
  void StaticInit();  // Emulates static initalization (tsan invisible).
  void Destroy();
  void Lock();
  bool TryLock();
  void Unlock();
  void ReadLock();
  bool TryReadLock();
  void ReadUnlock();

 private:
  // Placeholder for pthread_mutex_t, CRITICAL_SECTION or whatever.
  void *mtx_[128];
  bool alive_;
  const Type type_;

  Mutex(const Mutex&);
  void operator = (const Mutex&);
};

// A thread is started in CTOR and joined in DTOR.
class ScopedThread {
 public:
  explicit ScopedThread(bool detached = false, bool main = false);
  ~ScopedThread();
  void Detach();

  const ReportDesc *Access(void *addr, bool is_write, int size,
                           bool expect_race);
  const ReportDesc *Read(const MemLoc &ml, int size, bool expect_race = false) {
    return Access(ml.loc(), false, size, expect_race);
  }
  const ReportDesc *Write(const MemLoc &ml, int size,
                          bool expect_race = false) {
    return Access(ml.loc(), true, size, expect_race);
  }
  const ReportDesc *Read1(const MemLoc &ml, bool expect_race = false) {
    return Read(ml, 1, expect_race); }
  const ReportDesc *Read2(const MemLoc &ml, bool expect_race = false) {
    return Read(ml, 2, expect_race); }
  const ReportDesc *Read4(const MemLoc &ml, bool expect_race = false) {
    return Read(ml, 4, expect_race); }
  const ReportDesc *Read8(const MemLoc &ml, bool expect_race = false) {
    return Read(ml, 8, expect_race); }
  const ReportDesc *Write1(const MemLoc &ml, bool expect_race = false) {
    return Write(ml, 1, expect_race); }
  const ReportDesc *Write2(const MemLoc &ml, bool expect_race = false) {
    return Write(ml, 2, expect_race); }
  const ReportDesc *Write4(const MemLoc &ml, bool expect_race = false) {
    return Write(ml, 4, expect_race); }
  const ReportDesc *Write8(const MemLoc &ml, bool expect_race = false) {
    return Write(ml, 8, expect_race); }

  void Call(void(*pc)());
  void Return();

  void Create(const Mutex &m);
  void Destroy(const Mutex &m);
  void Lock(const Mutex &m);
  bool TryLock(const Mutex &m);
  void Unlock(const Mutex &m);
  void ReadLock(const Mutex &m);
  bool TryReadLock(const Mutex &m);
  void ReadUnlock(const Mutex &m);

  const ReportDesc *Memcpy(void *dst, const void *src, int size,
                           bool expect_race = false);
  const ReportDesc *Memset(void *dst, int val, int size,
                           bool expect_race = false);
 private:
  struct Impl;
  Impl *impl_;
  ScopedThread(const ScopedThread&);  // Not implemented.
  void operator = (const ScopedThread&);  // Not implemented.
};

class MainThread : public ScopedThread {
 public:
  MainThread()
    : ScopedThread(false, true) {
  }
};

const ReportDesc *GetLastReport();

#endif  // #ifndef TSAN_TEST_UTIL_H
