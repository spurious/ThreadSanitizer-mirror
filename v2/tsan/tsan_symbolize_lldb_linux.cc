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

static ReportStack *NewFrame(RegionAlloc *alloc, uptr addr) {
  ReportStack *ent = alloc->Alloc<ReportStack>(1);
  internal_memset(ent, 0, sizeof(*ent));
  ent->pc = addr;
  return ent;
}

static char *BuildFilename(RegionAlloc *alloc, SBFileSpec f) {
  if (!f.IsValid())
    return 0;
  const char *file = f.GetFilename();
  const char *dir = f.GetDirectory();
  int filesz = strlen(file);
  int dirsz = strlen(dir);
  char *fstr = alloc->Alloc<char>(filesz + dirsz + 2);
  internal_strcpy(fstr, dir);  // NOLINT
  fstr[dirsz] = '/';
  internal_strcpy(fstr + dirsz + 1, file);  // NOLINT
  return fstr;
}

ReportStack *SymbolizeCode(RegionAlloc *alloc, uptr addr) {
  LldbContext *ctx = GetContext();
  if (ctx == 0)
    return NewFrame(alloc, addr);

  // Resolve the address.
  SBAddress saddr = ctx->target.ResolveLoadAddress((addr_t)addr);
  if (!saddr.IsValid())
    return NewFrame(alloc, addr);
  SBSymbolContext sctx(ctx->target.ResolveSymbolContextForAddress(saddr,
      eSymbolContextFunction|eSymbolContextBlock
      |eSymbolContextLineEntry|eSymbolContextModule));
  if (!sctx.IsValid())
    return NewFrame(alloc, addr);

  // Extract module+offset.
  char *module = 0;
  uptr offset = 0;
  if (sctx.GetModule().IsValid() && sctx.GetModule().GetFileSpec().IsValid()) {
    module = alloc->Strdup(sctx.GetModule().GetFileSpec().GetFilename());
    offset = (uptr)saddr.GetFileAddress();
  }

  ReportStack *head = 0;
  ReportStack *tail = 0;
  SBBlock block = sctx.GetBlock();
  SBBlock pblock;
  for (; block.IsValid(); block = block.GetParent()) {
    if (!block.IsInlined())
      continue;
    ReportStack *ent = NewFrame(alloc, addr);
    ent->module = module;
    ent->offset = offset;
    ent->func = alloc->Strdup(block.GetInlinedName());
    if (pblock.IsValid()) {
      ent->file = BuildFilename(alloc, pblock.GetInlinedCallSiteFile());
      ent->line = pblock.GetInlinedCallSiteLine();
      ent->col = pblock.GetInlinedCallSiteColumn();
    } else {
      ent->file = BuildFilename(alloc, sctx.GetLineEntry().GetFileSpec());
      ent->line = sctx.GetLineEntry().GetLine();
      ent->col = sctx.GetLineEntry().GetColumn();
    }
    if (head == 0)
      head = ent;
    else
      tail->next = ent;
    tail = ent;
    pblock = block;
  }

  ReportStack *ent = NewFrame(alloc, addr);
  ent->module = module;
  ent->offset = offset;

  // Extract function name.
  const char* fname = 0;
  if (sctx.GetFunction().IsValid())
    fname = sctx.GetFunction().GetName();
  if (!fname && sctx.GetBlock().IsValid())
    fname = sctx.GetBlock().GetInlinedName();
  ent->func = alloc->Strdup(fname);

  // Extract file:line.
  if (sctx.GetLineEntry().IsValid()) {
    if (pblock.IsValid()) {
      ent->file = BuildFilename(alloc, pblock.GetInlinedCallSiteFile());
      ent->line = pblock.GetInlinedCallSiteLine();
      ent->col = pblock.GetInlinedCallSiteColumn();
    } else {
      ent->file = BuildFilename(alloc, sctx.GetLineEntry().GetFileSpec());
      ent->line = sctx.GetLineEntry().GetLine();
      ent->col = sctx.GetLineEntry().GetColumn();
    }
  }
  if (head == 0)
    head = ent;
  else
    tail->next = ent;
  tail = ent;
  return head;
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
