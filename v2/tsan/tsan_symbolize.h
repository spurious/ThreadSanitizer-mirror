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

namespace __tsan {

bool SymbolizeCode(uptr pc, char *func, int func_size,
                   char *file, int file_size, int *line);

}  // namespace __tsan

#endif  // TSAN_SYMBOLIZE_H
