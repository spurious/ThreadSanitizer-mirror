//===-- tsan_flags.h --------------------------------------------*- C++ -*-===//
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

#ifndef TSAN_FLAGS_H
#define TSAN_FLAGS_H

#include "tsan_defs.h"

namespace __tsan {

struct Flags {
  // Supress a race reports if we've already output another race report
  // with the same stacks.
  bool suppress_equal_stacks;
  // Supress a race reports if we've already output another race report
  // on the same address.
  bool suppress_equal_addresses;
};

Flags *flags();
void FlagsInit(Flags *flags);
}

#endif  // #ifndef TSAN_FLAGS_H
