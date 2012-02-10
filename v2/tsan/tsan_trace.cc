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
#include "tsan_rtl.h"

namespace __tsan {

TraceSet::TraceSet() {
  internal_memset(this, 0, sizeof(*this));
  curtrace = 0;
  pos = &traces[curtrace].events[0];
  end = pos + kTraceSize;
}

void TraceSet::Switch() {
  curtrace = (curtrace + 1) % kTraceCnt;
  pos = &traces[curtrace].events[0];
  end = pos + kTraceSize;
}

}  // namespace __tsan
