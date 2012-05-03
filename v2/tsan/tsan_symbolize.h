//===-- tsan_symbolize.h ----------------------------------------*- C++ -*-===//
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
#ifndef TSAN_SYMBOLIZE_H
#define TSAN_SYMBOLIZE_H

#include "tsan_defs.h"
#include "tsan_report.h"

namespace __tsan {

struct Symbol {
  char *module;
  uptr offset;
  char *name;
  char *file;
  int line;
  int col;
};

ReportStack *SymbolizeCode(RegionAlloc *alloc, uptr addr);
int SymbolizeData(RegionAlloc *alloc, uptr addr, Symbol *symb);

}  // namespace __tsan

#endif  // TSAN_SYMBOLIZE_H
