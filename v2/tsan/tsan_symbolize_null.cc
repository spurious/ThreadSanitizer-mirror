//===-- tsan_symbolize_null.cc ----------------------------------*- C++ -*-===//
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
#include "tsan_symbolize.h"
#include "tsan_rtl.h"

namespace __tsan {

ReportStack *SymbolizeCode(uptr addr) {
  ReportStack *stack = (ReportStack*)internal_alloc(MBlockTypeStack,
                                                    sizeof(ReportStack));
  internal_memset(stack, 0, sizeof(*stack));
  stack->pc = addr;
  return stack;
}

ReportStack *SymbolizeData(uptr addr) {
  (void)addr;
  return 0;
}

}  // namespace __tsan
