//===-- tsan_sync.cc --------------------------------------------*- C++ -*-===//
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
#include "tsan_sync.h"
#include "tsan_rtl.h"

namespace __tsan {

SyncVar::SyncVar(SyncVar::Type type, uptr addr)
  : type(type)
  , addr(addr) {
}

void SyncVar::Read(ThreadState *thr, uptr pc) {
  MemoryAccess(thr, pc, addr, 1, false);
}

void SyncVar::Write(ThreadState *thr, uptr pc) {
  MemoryAccess(thr, pc, addr, 1, true);
}

MutexVar::MutexVar(uptr addr, bool is_rw)
  : SyncVar(SyncVar::Mtx, addr)
  , is_rw(is_rw) {
}

SyncTab::SyncTab() {
}

void SyncTab::insert(SyncVar *var) {
  CHECK(tab_.Insert(var->addr, var));
}

SyncVar* SyncTab::GetAndLockIfExists(uptr addr) {
  Lock l(&mtx_);
  SyncVar *res;
  if (!tab_.Get(addr, &res))
    return 0;
  CHECK(res);
  res->mtx.Lock();
  return res;
}

SyncVar* SyncTab::GetAndRemoveIfExists(uptr addr) {
  Lock l(&mtx_);
  SyncVar *res;
  if (!tab_.Get(addr, &res))
    return 0;
  CHECK(res);
  CHECK(tab_.Erase(addr));
  return res;
}

}  // namespace __tsan
