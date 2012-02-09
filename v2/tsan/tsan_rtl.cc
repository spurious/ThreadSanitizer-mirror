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

static void Initialize() {
  Printf("tsan::Initialize\n");
  InitializeShadowMemory();
  InitializeInterceptors();
}

ALWAYS_INLINE
static void OnMemoryAccess(void *addr, int size, bool is_write) {
  uptr app_mem = (uptr)addr;
  uptr shadow_mem = MemToShadow(app_mem);
  Printf("tsan::OnMemoryAccess: %p size=%d is_write=%d shadow_mem=%p\n",
      app_mem, size, is_write, shadow_mem);
  CHECK(IsAppMem(app_mem));
  CHECK(IsShadowMem(shadow_mem));
  u64 *shadow_array = (u64*)shadow_mem;
  for (uptr i = 0; i < TSAN_SHADOW_STATE_LENGTH; i++) {
    u64 shadow = shadow_array[i];
    Printf("  [%ld] %llx\n", i, shadow);
  }
}

ALWAYS_INLINE
static void FuncEntry(void *call_pc) {
  Printf("tsan::FuncEntry %p\n", call_pc);
}

ALWAYS_INLINE
static void FuncExit() {
  Printf("tsan::FuncExit\n");
}

}  // namespace __tsan

// ----------------- Interface functions    --------------------------------{{{1
void __tsan_init() { __tsan::Initialize(); }

void __tsan_read1(void *addr) { __tsan::OnMemoryAccess(addr, 1, false); }
void __tsan_read2(void *addr) { __tsan::OnMemoryAccess(addr, 2, false); }
void __tsan_read4(void *addr) { __tsan::OnMemoryAccess(addr, 4, false); }
void __tsan_read8(void *addr) { __tsan::OnMemoryAccess(addr, 8, false); }
void __tsan_read16(void *addr) {
  __tsan::OnMemoryAccess(addr, 8, false);
  __tsan::OnMemoryAccess((char*)addr + 8, 8, false);
}

void __tsan_write1(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write2(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write4(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write8(void *addr) { __tsan::OnMemoryAccess(addr, 1, true); }
void __tsan_write16(void *addr) {
  __tsan::OnMemoryAccess(addr, 8, true);
  __tsan::OnMemoryAccess((char*)addr + 8, 8, true);
}

void __tsan_func_entry(void *call_pc) { __tsan::FuncEntry(call_pc); }
void __tsan_func_exit() { __tsan::FuncExit(); }

// ----------------- ifdef clatter --------------------------------{{{1
// We tolerate a bit of ifdef/include clatter at the very bottom of this file
// in order to have performance-critical code inlined.
#ifdef __linux__
namespace __tsan {
static __thread ThreadState tls_thread_state;
ALWAYS_INLINE
ThreadState GetThreadState() {
  return tls_thread_state;
}
ALWAYS_INLINE
void SetThreadState(ThreadState thread_state) {
  tls_thread_state = thread_state;
}
}  // namespace __tsan
#else
# error "This platform is unsupported yet"
#endif

