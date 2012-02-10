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

namespace __tsan {

SyncVar::SyncVar(SyncVar::Type type, uptr addr)
  : type(type)
  , addr(addr) {
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

SyncVar* SyncTab::get_and_lock(uptr addr) {
  Lock l(&mtx_);
  SyncVar *res;
  tab_.Get(addr, &res);
  CHECK(res);
  res->mtx.Lock();
  return res;
}

SyncVar* SyncTab::get_and_remove(uptr addr) {
  Lock l(&mtx_);
  SyncVar *res;
  tab_.Get(addr, &res);
  CHECK(res);
  CHECK(tab_.Erase(addr));
  return res;
}

}  // namespace __tsan
