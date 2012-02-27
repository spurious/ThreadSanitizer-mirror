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
#include "tsan_rtl.h"

#include <lldb/API/SBBlock.h>
#include <lldb/API/SBCompileUnit.h>
#include <lldb/API/SBDebugger.h>
#include <lldb/API/SBFunction.h>
#include <lldb/API/SBModule.h>
#include <lldb/API/SBSymbol.h>
#include <lldb/API/SBTarget.h>
#include <lldb/API/SBThread.h>
#include <lldb/API/SBProcess.h>
#include <lldb/API/SBStream.h>
#include <lldb/Interpreter/Args.h>
#include <lldb/Core/StreamFile.h>
#include <lldb/Symbol/SymbolContextScope.h>

#include <link.h>
#include <string.h>
#include <errno.h>

using namespace lldb;  // NOLINT

namespace __tsan {

struct ScopedErrno {
  int err;
  ScopedErrno() {
    this->err = errno;
  }
  ~ScopedErrno() {
    errno = this->err;
  }
};

struct LldbContext {
  bool is_valid;
  bool is_first;
  SBDebugger debugger;
  SBTarget target;
};

static int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *ctx0) {
  LldbContext *ctx = (LldbContext*)ctx0;
  const bool is_first = ctx->is_first;
  ctx->is_first = false;
  if (is_first)
    info->dlpi_name = "/proc/self/exe";
  if (info->dlpi_name == 0)
    return 0;
  SBModule module = ctx->target.AddModule(info->dlpi_name, 0, 0);
  if (!module.IsValid())
    return 0;
  for (size_t i = 0; i < module.GetNumSections(); i++) {
    SBSection sect = module.GetSectionAtIndex(i);
    SectionType type = sect.GetSectionType();
    if (type == eSectionTypeCode
        || type == eSectionTypeData
        || type == eSectionTypeZeroFill) {
      addr_t start = (addr_t)info->dlpi_addr + sect.GetFileAddress();
      ctx->target.SetSectionLoadAddress(sect, start);
    }
  }
  return 0;
}

static LldbContext *GetContext() {
  static LldbContext *ctx;
  if (ctx)
    return ctx->is_valid ? ctx : 0;
  ctx = new LldbContext;
  ctx->is_valid = false;
  ctx->is_first = true;
  SBDebugger::Initialize();
  ctx->debugger = SBDebugger::Create();
  if (!ctx->debugger.IsValid())
    return 0;
  ctx->target = ctx->debugger.CreateTarget("/proc/self/exe");
  if (!ctx->target.IsValid())
    return 0;
  if (dl_iterate_phdr(dl_iterate_phdr_cb, ctx))
    return 0;
  ctx->is_valid = true;
  return ctx;
}

int SymbolizeCode(RegionAlloc *alloc, uptr addr, Symbol *symb, int cnt) {
  ScopedErrno se;
  LldbContext *ctx = GetContext();
  if (ctx == 0)
    return 0;

  // Resolve the address.
  SBAddress saddr = ctx->target.ResolveLoadAddress((addr_t)addr);
  if (!saddr.IsValid())
    return 0;
  SBSymbolContext sctx(ctx->target.ResolveSymbolContextForAddress(saddr,
      eSymbolContextFunction|eSymbolContextBlock|eSymbolContextLineEntry));
  if (!sctx.IsValid())
    return 0;

  // Extract function name.
  const char* fname = 0;
  if (sctx.GetBlock().IsValid())
    fname = sctx.GetBlock().GetInlinedName();
  if (!fname && sctx.GetFunction().IsValid())
    fname = sctx.GetFunction().GetName();
  symb[0].name = 0;
  if (fname) {
    int fnamesz = strlen(fname);
    symb[0].name = alloc->Alloc<char>(fnamesz + 1);
    strcpy(symb[0].name, fname);  // NOLINT
  }

  // Extract file:line.
  if (sctx.GetLineEntry().IsValid()) {
    if (sctx.GetLineEntry().GetFileSpec().IsValid()) {
      const char *file = sctx.GetLineEntry().GetFileSpec().GetFilename();
      const char *dir = sctx.GetLineEntry().GetFileSpec().GetDirectory();
      int filesz = strlen(file);
      int dirsz = strlen(dir);
      symb[0].file = alloc->Alloc<char>(filesz + dirsz + 2);
      strcpy(symb[0].file, dir);  // NOLINT
      symb[0].file[dirsz] = '/';
      strcpy(symb[0].file + dirsz + 1, file);  // NOLINT
    }
    symb[0].line = sctx.GetLineEntry().GetLine();
  }
  (void)cnt;
  return 1;
}

int SymbolizeData(RegionAlloc *alloc, uptr addr, Symbol *symb) {
  ScopedErrno se;
  LldbContext *ctx = GetContext();
  if (ctx == 0)
    return 0;
  (void)alloc;
  (void)addr;
  (void)symb;
  return 0;
}

}  // namespace __tsan
