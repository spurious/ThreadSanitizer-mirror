//===-- tsan_trace.cc ------------------------------------------*- C++ -*-===//
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
#include "tsan_trace.h"

namespace __tsan {

void NOINLINE TraceSet::Switch() {
  pos = (pos+1) % kTraceCnt;
  tracepos = 0;
}

void TraceSet::AddEvent(EventType typ, uptr addr) {
  if (tracepos == kTraceSize)
    Switch();
  Event &ev = traces[pos].events[tracepos];
  ev.typ = typ;
  ev.addr = addr;
  tracepos++;
}

}  // namespace __tsan
