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

extern FILE* G_out;

typedef uintptr_t pc_t;
typedef intptr_t tid_t;
tid_t GetTid();
pc_t GetPc();

void ReadDbgInfo(string filename);

#define DECLARE_TID() \
  tid_t tid = GetTid();

#define DECLARE_TID_AND_PC() \
  tid_t tid = GetTid(); \
  pc_t pc = GetPc();

int unsafe_clear_pending_signals();
void flush_tleb();
static inline void Put(EventType type, tid_t tid, pc_t pc,
                       uintptr_t a, uintptr_t info);

// Put a synchronization event to ThreadSanitizer.
static inline void SPut(EventType type, tid_t tid, pc_t pc,
                        uintptr_t a, uintptr_t info);

#include "tsan_rtl_wrap.h"

#endif  // TSAN_RTL_H_
