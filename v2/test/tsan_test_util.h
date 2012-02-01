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

// A location of memory on which a race may be detected.
class MemLoc {
 public:
  explicit MemLoc(int offset_from_aligned = 0);
  ~MemLoc();
  void *loc() const { return loc_; }
 private:
  void *loc_;
};

// A thread is started in CTOR and joined in DTOR.
class ScopedThread {
 public:
  ScopedThread();
  ~ScopedThread();
  void Access(const MemLoc &ml, bool is_write, int size, bool expect_race);
  void Read(const MemLoc &ml, int size, bool expect_race) {
    Access(ml, false, size, expect_race);
  }
  void Write(const MemLoc &ml, int size, bool expect_race) {
    Access(ml, true, size, expect_race);
  }
  template <int size> void Read(const MemLoc &ml, bool expect_race = false) {
    Read(ml, size, expect_race);
  }
  template <int size> void Write(const MemLoc &ml, bool expect_race = false) {
    Write(ml, size, expect_race);
  }
 private:
  struct Impl;
  Impl *impl_;
};
