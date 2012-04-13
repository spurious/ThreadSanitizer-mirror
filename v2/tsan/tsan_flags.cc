//===-- tsan_flags.cc -------------------------------------------*- C++ -*-===//
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

#include "tsan_flags.h"
#include "tsan_rtl.h"

namespace __tsan {

Flags *flags() {
  return &CTX()->flags;
}

void FlagsInit(Flags *flags) {
  flags->enable_annotations = true;
  flags->suppress_equal_stacks = true;
  flags->suppress_equal_addresses = true;
  flags->report_thread_leaks = true;
}
}
