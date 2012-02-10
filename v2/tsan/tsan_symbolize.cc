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

bool SymbolizeCode(uptr pc, char *func, int func_size,
                   char *file, int file_size, int *line) {
  bool res = false;
  FILE *f = fopen("/proc/self/cmdline", "rb");
  if (f) {
    char exe[1024];
    fread(exe, sizeof(exe), 1, f);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "nm %s|grep SymbolizeCode > tsan.tmp", exe);
    system(cmd);
    FILE* f2 = fopen("tsan.tmp", "rb");
    if (f2) {
      char tmp[1024];
      fread(tmp, sizeof(tmp), 1, f2);
      uptr addr = (uptr)strtoll(tmp, 0, 16);
      uptr base = (uptr)&SymbolizeCode - addr;
      snprintf(cmd, sizeof(cmd),
          "addr2line -C -s -f -e %s %p > tsan.tmp2", exe, (void*)(pc - base));
      system(cmd);
      FILE* f3 = fopen("tsan.tmp2", "rb");
      if (f3) {
        fread(tmp, sizeof(tmp), 1, f3);
        char *pos = strchr(tmp, '\n');
        if (pos) {
          internal_memcpy(func, tmp, pos - tmp);
          func[pos - tmp] = 0;
          char *pos2 = strchr(pos, ':');
          if (pos2) {
            internal_memcpy(file, pos + 1, pos2 - pos - 1);
            file[pos2 - pos - 1] = 0;
            *line = atoi(pos2 + 1);
          }
          res = true;
        }
        fclose(f3);
      }
      fclose(f2);
    }
    fclose(f);
  }
  return res;
}

}  // namespace __tsan
