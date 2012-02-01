//===-- tsan_linux.cc -------------------------------------------*- C++ -*-===//
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
// Linux-specific code.
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"

#include <stdio.h>
#include <stdarg.h>

namespace __tsan {

void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

}  // namespace __tsan
