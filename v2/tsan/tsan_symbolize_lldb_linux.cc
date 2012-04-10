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

struct LldbContext {
  bool is_valid;
  bool is_first;
  SBDebugger debugger;
  SBTarget target;
};

static int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *ctx0) {
  LldbContext *ctx = (LldbContext*)ctx0;
  if (ctx->is_first)
    info->dlpi_name = "/proc/self/exe";
  ctx->is_first = false;
  if (info->dlpi_name == 0)
    return 0;
  SBModule module = ctx->target.AddModule(info->dlpi_name, 0, 0);
  if (!module.IsValid())
    return 0;
  ctx->target.SetModuleLoadAddress(module, (addr_t)info->dlpi_addr);
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

ReportStack *SymbolizeCode(RegionAlloc *alloc, uptr addr) {
  ReportStack *ent = alloc->Alloc<ReportStack>(1);
  internal_memset(ent, 0, sizeof(*ent));
  ent->pc = addr;

  LldbContext *ctx = GetContext();
  if (ctx == 0)
    return 0;

  // Resolve the address.
  SBAddress saddr = ctx->target.ResolveLoadAddress((addr_t)addr);
  if (!saddr.IsValid())
    return 0;
  SBSymbolContext sctx(ctx->target.ResolveSymbolContextForAddress(saddr,
      eSymbolContextFunction|eSymbolContextBlock
      |eSymbolContextLineEntry|eSymbolContextModule));
  if (!sctx.IsValid())
    return 0;

  // Extract module+offset.
  ent->module = alloc->Strdup(sctx.GetModule().GetFileSpec().GetFilename());
  ent->offset = (uptr)saddr.GetFileAddress();

  // Extract function name.
  const char* fname = 0;
  if (sctx.GetBlock().IsValid())
    fname = sctx.GetBlock().GetInlinedName();
  if (!fname && sctx.GetFunction().IsValid())
    fname = sctx.GetFunction().GetName();
  ent->func = alloc->Strdup(fname);

  // Extract file:line.
  if (sctx.GetLineEntry().IsValid()) {
    if (sctx.GetLineEntry().GetFileSpec().IsValid()) {
      const char *file = sctx.GetLineEntry().GetFileSpec().GetFilename();
      const char *dir = sctx.GetLineEntry().GetFileSpec().GetDirectory();
      int filesz = strlen(file);
      int dirsz = strlen(dir);
      ent->file = alloc->Alloc<char>(filesz + dirsz + 2);
      internal_strcpy(ent->file, dir);  // NOLINT
      ent->file[dirsz] = '/';
      internal_strcpy(ent->file + dirsz + 1, file);  // NOLINT
    }
    ent->line = sctx.GetLineEntry().GetLine();
  }
  return ent;
}

int SymbolizeData(RegionAlloc *alloc, uptr addr, Symbol *symb) {
  LldbContext *ctx = GetContext();
  if (ctx == 0)
    return 0;
  (void)alloc;
  (void)addr;
  (void)symb;
  return 0;
}

}  // namespace __tsan
