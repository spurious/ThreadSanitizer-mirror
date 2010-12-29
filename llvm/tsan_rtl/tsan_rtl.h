// Copyright 2010 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#ifndef TSAN_RTL_H_
#define TSAN_RTL_H_

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <stdio.h>
#include "string"

#include "thread_sanitizer.h"
#include "suppressions.h"
#include "ts_util.h"

// #define DEBUG 1
#undef DEBUG

#ifdef DEBUG
#define CHECK_IN_RTL() do { \
  assert((IN_RTL >= 0) && (IN_RTL <= 5)); \
} while (0)
#else
#define CHECK_IN_RTL()
#endif


int unsafe_clear_pending_signals();

class GIL {
 public:
  GIL() {
    Lock();
  }
  ~GIL() {
    Unlock();
  }

  static void Lock();

  static bool TryLock();
  static void Unlock();
#ifdef DEBUG
  static int GetDepth();
#endif
};

extern FILE* G_out;

typedef uintptr_t pc_t;
typedef intptr_t tid_t;
tid_t GetTid();
pc_t GetPc();

void ReadElf();
void AddWrappersDbgInfo();
void ReadDbgInfo(string filename);

#define DECLARE_TID() \
  tid_t tid = GetTid();

#define DECLARE_TID_AND_PC() \
  tid_t tid = GetTid(); \
  pc_t pc = GetPc();

#define EX_DECLARE_TID_AND_PC() \
  tid_t tid = ExGetTid(); \
  pc_t pc = ExGetPc();

void MaybeInitTid();
void flush_tleb();
extern "C" void rtn_call(void *addr);
extern "C" void rtn_exit();

inline void Put(EventType type, tid_t tid, pc_t pc,
                uintptr_t a, uintptr_t info);
inline void SPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info);
inline void RPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info);

// Put a synchronization event to ThreadSanitizer.
extern void ExPut(EventType type, tid_t tid, pc_t pc,
                        uintptr_t a, uintptr_t info);
extern void ExSPut(EventType type, tid_t tid, pc_t pc,
                        uintptr_t a, uintptr_t info);
extern void ExRPut(EventType type, tid_t tid, pc_t pc,
                        uintptr_t a, uintptr_t info);


void IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN(void);
void IGNORE_ALL_ACCESSES_AND_SYNC_END(void);
extern tid_t ExGetTid();
extern pc_t ExGetPc();

#include "tsan_rtl_wrap.h"

#endif  // TSAN_RTL_H_
