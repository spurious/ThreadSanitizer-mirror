//===-- tsan_symbolize.cc ---------------------------------------*- C++ -*-===//
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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace __tsan {

static char exe[1024];
static uptr base;

void SymbolizeImageBaseMark() {
}

static uptr GetImageBase() {
  uptr base = 0;
  FILE *f = fopen("/proc/self/cmdline", "rb");
  if (f) {
    if (fread(exe, 1, sizeof(exe), f) <= 0)
      return 0;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "nm %s|grep SymbolizeImageBaseMark > tsan.tmp",
             exe);
    if (system(cmd))
      return 0;
    FILE* f2 = fopen("tsan.tmp", "rb");
    if (f2) {
      char tmp[1024];
      if (fread(tmp, 1, sizeof(tmp), f2) <= 0)
        return 0;
      uptr addr = (uptr)strtoll(tmp, 0, 16);
      base = (uptr)&SymbolizeImageBaseMark - addr;
      fclose(f2);
    }
    fclose(f);
  }
  return base;
}

bool SymbolizeCode(uptr pc, char *func, int func_size,
                   char *file, int file_size, int *line) {
  if (base == 0)
    base = GetImageBase();
  bool res = false;
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "addr2line -C -s -f -e %s %p > tsan.tmp2", exe, (void*)(pc - base));
  if (system(cmd))
    return 0;
  FILE* f3 = fopen("tsan.tmp2", "rb");
  if (f3) {
    char tmp[1024];
    if (fread(tmp, 1, sizeof(tmp), f3) <= 0)
      return 0;
    char *pos = strchr(tmp, '\n');
    if (pos) {
      res = true;
      internal_memcpy(func, tmp, pos - tmp);
      func[pos - tmp] = 0;
      char *pos2 = strchr(pos, ':');
      if (pos2) {
        internal_memcpy(file, pos + 1, pos2 - pos - 1);
        file[pos2 - pos - 1] = 0;
        *line = atoi(pos2 + 1);
      }
    }
    fclose(f3);
  }
  return res;
}

}  // namespace __tsan
