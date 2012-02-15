//===-- tsan_mutex_test.cc --------------------------------------*- C++ -*-===//
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
#include "gtest/gtest.h"

namespace __tsan {

class TestData {
 public:
  TestData() {
    for (int i = 0; i < kSize; i++)
      data_[i] = 0;
  }

  void Write() {
    Lock l(&mtx_);
    T v0 = data_[0];
    for (int i = 0; i < kSize; i++) {
      CHECK_EQ(data_[i], v0);
      data_[i]++;
    }
  }

  void Read() {
    ReadLock l(&mtx_);
    T v0 = data_[0];
    for (int i = 0; i < kSize; i++) {
      CHECK_EQ(data_[i], v0);
    }
  }

 private:
  static const int kSize = 16;
  typedef u64 T;
  T data_[kSize];
  Mutex mtx_;
};

const int kThreads = 8;
const int kWriteRate = 1024;
#if TSAN_DEBUG
const int kIters = 128*1024;
#else
const int kIters = 512*1024;
#endif

static void *write_mutex_thread(void *param) {
  TestData *data = (TestData *)param;
  for (int i = 0; i < kIters; i++)
    data->Write();
  return 0;
}

static void *read_mutex_thread(void *param) {
  TestData *data = (TestData *)param;
  for (int i = 0; i < kIters; i++) {
    if ((i % kWriteRate) == 0)
      data->Write();
    else
      data->Read();
  }
  return 0;
}

TEST(Mutex, Write) {
  TestData data;
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    pthread_create(&threads[i], 0, write_mutex_thread, &data);
  for (int i = 0; i < kThreads; i++)
    pthread_join(threads[i], 0);
}

TEST(Mutex, ReadWrite) {
  TestData data;
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    pthread_create(&threads[i], 0, read_mutex_thread, &data);
  for (int i = 0; i < kThreads; i++)
    pthread_join(threads[i], 0);
}

}  // namespace __tsan
