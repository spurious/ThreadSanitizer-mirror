//===-- tsan_rtl.cc ---------------------------------------------*- C++ -*-===//
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
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "tsan_linux.h"
#include "tsan_rtl.h"
#include "tsan_interface.h"

namespace __tsan {

void CheckFailed(const char *file, int line, const char *cond) {
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\"\n",
      file, line, cond);
}

void Initialize() {
  Printf("tsan::Initialize\n");
  InitializeShadowMemory();
  // InitializeInterceptors();
}

void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write) {
  uptr app_mem = (uptr)addr;
  uptr shadow_mem = MemToShadow(app_mem);
  Printf("#%d: tsan::OnMemoryAccess: @%p %p size=%d"
         " is_write=%d shadow_mem=%p\n",
         (int)thr->id, (void*)pc, (void*)app_mem, size, is_write, shadow_mem);
  CHECK(IsAppMem(app_mem));
  CHECK(IsShadowMem(shadow_mem));
  u64 *shadow_array = (u64*)shadow_mem;
  for (uptr i = 0; i < TSAN_SHADOW_STATE_LENGTH; i++) {
    u64 shadow = shadow_array[i];
    Printf("  [%ld] %llx\n", i, shadow);
  }
}

void FuncEntry(ThreadState *thr, uptr pc) {
  Printf("#%d: tsan::FuncEntry %p\n", (int)thr->id, (void*)pc);
}

void FuncExit(ThreadState *thr) {
  Printf("#%d: tsan::FuncExit\n", (int)thr->id);
}

}  // namespace __tsan

