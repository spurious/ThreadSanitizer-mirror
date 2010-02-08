/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Konstantin Serebryany.

#ifndef TS_LOCK_H_
#define TS_LOCK_H_

//--------- Simple Lock ------------------ {{{1
#ifdef TS_PIN
#include "pin.H"
class TSLock {
 public:
  TSLock() {
    InitLock(&lock_);
  }
  void Lock() {
    GetLock(&lock_, __LINE__);
  }
  void Unlock() {
    ReleaseLock(&lock_);
  }

 private:
  PIN_LOCK lock_;
};
#else
# error "Not supported locking"
#endif

class ScopedLock {
 public:
  ScopedLock(TSLock *lock)
    : lock_(lock) {
    lock_->Lock();
  }
  ~ScopedLock() { lock_->Unlock(); }
 private:
  TSLock *lock_;
};


// end. {{{1
#endif  // TS_LOCK_H_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
