//===-- tsan_trace.h -------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_TRACE_H
#define TSAN_TRACE_H

#include "tsan_defs.h"

namespace __tsan {

const int kTraceCnt = 8;
const int kTraceSize = 1024;

// Must fit into 3 bits.
enum EventType {
  EventTypeMop,
  EventTypeFuncEnter,
  EventTypeFuncExit,
  EventTypeLock,
  EventTypeUnlock,
  EventTypeRLock,
  EventTypeRUnlock,
};

// Represents a thread event.
// u64 addr : 61;  // Associated pc.
// u64 typ  : 3;   // EventType.
typedef u64 Event;

struct Trace {
  uptr  stack0[32];  // Start stack for the trace.
  u64   epoch0;      // Start epoch for the trace.
  Event events[kTraceSize];
};

struct TraceSet {
  Event *pos;
  Event *end;
  int curtrace;
  Trace traces[kTraceCnt];

  TraceSet();

  void AddEvent(EventType typ, uptr addr) {
    if (pos == end)
      Switch();
    *pos = (u64)addr | ((u64)typ << 61);
    pos++;
  }
 private:
  void Switch() NOINLINE;
};

}  // namespace __tsan

#endif  // TSAN_TRACE_H
