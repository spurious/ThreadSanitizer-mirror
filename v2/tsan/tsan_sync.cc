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
#include "tsan_placement_new.h"
#include "tsan_rtl.h"

namespace __tsan {

SyncVar::SyncVar(uptr addr)
  : mtx(StatMtxSyncVar)
  , addr(addr)
  , owner_tid(-1)
  , recursion()
  , is_rw()
  , is_recursive() {
}

SyncTab::Part::Part()
  : mtx(StatMtxSyncTab)
  , val() {
}

SyncTab::SyncTab() {
}

SyncVar* SyncTab::GetAndLock(SlabCache *slab, uptr addr, bool write_lock) {
  DCHECK(slab->Size() == sizeof(SyncVar));
  Part *p = &tab_[PartIdx(addr)];
  {
    ReadLock l(&p->mtx);
    for (SyncVar *res = p->val; res; res = res->next) {
      if (res->addr == addr) {
        if (write_lock)
          res->mtx.Lock();
        else
          res->mtx.ReadLock();
        return res;
      }
    }
  }
  {
    Lock l(&p->mtx);
    SyncVar *res = p->val;
    for (; res; res = res->next) {
      if (res->addr == addr)
        break;
    }
    if (res == 0) {
      res = new(slab->Alloc()) SyncVar(addr);
      res->next = p->val;
      p->val = res;
    }
    if (write_lock)
      res->mtx.Lock();
    else
      res->mtx.ReadLock();
    return res;
  }
}

SyncVar* SyncTab::GetAndRemove(uptr addr) {
  Part *p = &tab_[PartIdx(addr)];
  SyncVar *res = 0;
  {
    Lock l(&p->mtx);
    SyncVar **prev = &p->val;
    res = *prev;
    while (res) {
      if (res->addr == addr) {
        *prev = res->next;
        break;
      }
      prev = &res->next;
      res = *prev;
    }
  }
  if (res) {
    res->mtx.Lock();
    res->mtx.Unlock();
  }
  return res;
}

int SyncTab::PartIdx(uptr addr) {
  return (addr >> 3) % kPartCount;
}

}  // namespace __tsan
