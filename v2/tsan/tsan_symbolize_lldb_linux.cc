//===-- tsan_symbolize_lldb_linux.cc ----------------------------*- C++ -*-===//
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

namespace __tsan {

int SymbolizeCode(RegionAlloc *alloc, uptr addr, Symbol *symb, int cnt) {
  (void)alloc;
  (void)addr;
  (void)symb;
  (void)cnt;
  return 0;
}

int SymbolizeData(RegionAlloc *alloc, uptr addr, Symbol *symb) {
  (void)alloc;
  (void)addr;
  (void)symb;
  return 0;
}

}  // namespace __tsan
