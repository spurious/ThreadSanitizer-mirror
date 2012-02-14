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

SyncTab::Part::Part()
  : val() {
}

SyncTab::SyncTab() {
}

void SyncTab::insert(SyncVar *var) {
  Part *p = &tab_[PartIdx(var->addr)];
  Lock l(&p->mtx);
  var->next = p->val;
  p->val = var;
}

SyncVar* SyncTab::GetAndLockIfExists(uptr addr) {
  Part *p = &tab_[PartIdx(addr)];
  Lock l(&p->mtx);
  for (SyncVar *res = p->val; res; res = res->next) {
    if (res->addr == addr) {
      res->mtx.Lock();
      return res;
    }
  }
  return 0;
}

SyncVar* SyncTab::GetAndRemoveIfExists(uptr addr) {
  Part *p = &tab_[PartIdx(addr)];
  Lock l(&p->mtx);
  SyncVar **prev = &p->val;
  SyncVar *res = *prev;
  while (res) {
    if (res->addr == addr) {
      *prev = res->next;
      return res;
    }
    prev = &res->next;
  }
  return 0;
}

int SyncTab::PartIdx(uptr addr) {
  return (addr >> 3) % kPartCount;
}

}  // namespace __tsan
