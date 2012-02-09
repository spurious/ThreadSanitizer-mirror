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
  CHECK(tab_.insert(std::make_pair(var->addr, var)).second);
}

void SyncTab::remove(SyncVar *var) {
  CHECK(tab_.erase(var->addr));
}

SyncVar* SyncTab::get_and_lock(uptr addr) {
  Lock l(&mtx_);
  tab_t::iterator i = tab_.lower_bound(addr);
  CHECK(i != tab_.end() && i->first == addr);
  i->second->mtx.Lock();
  return i->second;
}

}  // namespace __tsan
