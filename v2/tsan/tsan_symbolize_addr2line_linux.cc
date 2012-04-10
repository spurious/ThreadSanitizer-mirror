//===-- tsan_symbolize_addr2line.cc -----------------------------*- C++ -*-===//
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
#include "tsan_mman.h"
#include "tsan_rtl.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
    InternalScopedBuf<char> cmd(1024);
    snprintf(cmd, cmd.Size(),
        "nm %s|grep SymbolizeImageBaseMark > tsan.tmp", exe);
    if (system(cmd))
      return 0;
    FILE* f2 = fopen("tsan.tmp", "rb");
    if (f2) {
      InternalScopedBuf<char> tmp(1024);
      if (fread(tmp, 1, tmp.Size(), f2) <= 0)
        return 0;
      uptr addr = (uptr)strtoll(tmp, 0, 16);
      base = (uptr)&SymbolizeImageBaseMark - addr;
      fclose(f2);
    }
    fclose(f);
  }
  return base;
}

ReportStack *SymbolizeCode(RegionAlloc *alloc, uptr addr) {
  if (base == 0)
    base = GetImageBase();
  ReportStack *res = alloc->Alloc<ReportStack>(1);
  internal_memset(res, 0, sizeof(*res));
  res->pc = addr;
  InternalScopedBuf<char> cmd(1024);
  snprintf(cmd, cmd.Size(),
           "addr2line -C -s -f -e %s %p > tsan.tmp2", exe,
           (void*)(addr - base));
  if (system(cmd))
    return res;
  FILE* f3 = fopen("tsan.tmp2", "rb");
  if (f3) {
    InternalScopedBuf<char> tmp(1024);
    if (fread(tmp, 1, tmp.Size(), f3) <= 0)
      return res;
    char *pos = strchr(tmp, '\n');
    if (pos && tmp[0] != '?') {
      res->func = alloc->Alloc<char>(pos - tmp + 1);
      internal_memcpy(res->func, tmp, pos - tmp);
      res->func[pos - tmp] = 0;
      char *pos2 = strchr(pos, ':');
      if (pos2) {
        res->file = alloc->Alloc<char>(pos2 - pos - 1 + 1);
        internal_memcpy(res->file, pos + 1, pos2 - pos - 1);
        res->file[pos2 - pos - 1] = 0;
        res->line = atoi(pos2 + 1);
      }
    }
    fclose(f3);
  }
  return res;
}

int SymbolizeData(RegionAlloc *alloc, uptr addr, Symbol *symb) {
  if (base == 0)
    base = GetImageBase();
  int res = 0;
  InternalScopedBuf<char> cmd(1024);
  snprintf(cmd, cmd.Size(),
  "nm -alsC %s|grep \"%lx\"|awk '{printf(\"%%s\\n%%s\", $3, $4)}' > tsan.tmp2",
    exe, (addr - base));
  if (system(cmd))
    return 0;
  FILE* f3 = fopen("tsan.tmp2", "rb");
  if (f3) {
    InternalScopedBuf<char> tmp(1024);
    if (fread(tmp, 1, tmp.Size(), f3) <= 0)
      return 0;
    char *pos = strchr(tmp, '\n');
    if (pos && tmp[0] != '?') {
      res = 1;
      symb[0].module = 0;
      symb[0].offset = addr;
      symb[0].name = alloc->Alloc<char>(pos - tmp + 1);
      internal_memcpy(symb[0].name, tmp, pos - tmp);
      symb[0].name[pos - tmp] = 0;
      symb[0].file = 0;
      symb[0].line = 0;
      char *pos2 = strchr(pos, ':');
      if (pos2) {
        symb[0].file = alloc->Alloc<char>(pos2 - pos - 1 + 1);
        internal_memcpy(symb[0].file, pos + 1, pos2 - pos - 1);
        symb[0].file[pos2 - pos - 1] = 0;
        symb[0].line = atoi(pos2 + 1);
      }
    }
    fclose(f3);
  }
  return res;
}

}  // namespace __tsan
