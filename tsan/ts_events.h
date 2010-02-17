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
// Author: Timur Iskhodzhanov.

#ifndef TS_EVENTS_H_
#define TS_EVENTS_H_

enum EventType {
  NOOP,
  READ,
  WRITE,
  READER_LOCK,
  WRITER_LOCK,
  UNLOCK,
  UNLOCK_OR_INIT,
  LOCK_CREATE,
  LOCK_DESTROY,
  BUS_LOCK_ACQUIRE,
  BUS_LOCK_RELEASE,
  THR_START,
  THR_FIRST_INSN,
  THR_END,
  THR_JOIN_AFTER,  // {tid, pc, joined_tid}
  THR_STACK_TOP,  // {tid, pc, stack_top, stack_size_if_known}
  RTN_EXIT,
  RTN_CALL,
  SBLOCK_ENTER,
  BBLOCK_ENTER,
  SIGNAL,
  WAIT_BEFORE,
  WAIT_AFTER,
  TWAIT_AFTER,
  CYCLIC_BARRIER_INIT,  // {tid, pc, obj, n}
  CYCLIC_BARRIER_WAIT_BEFORE,  // {tid, pc, obj, 0}
  CYCLIC_BARRIER_WAIT_AFTER,  // {tid, pc, obj, 0}
  PCQ_CREATE,
  PCQ_DESTROY,
  PCQ_PUT,
  PCQ_GET,
  SP_CHANGE,
  STACK_MEM_NEW,
  STACK_MEM_DIE,
  MALLOC,
  FREE,
  PUBLISH_RANGE,
  UNPUBLISH_RANGE,
  HB_LOCK,
  IGNORE_READS_BEG,
  IGNORE_READS_END,
  IGNORE_WRITES_BEG,
  IGNORE_WRITES_END,
  SET_THREAD_NAME,
  SET_LOCK_NAME,
  TRACE_MEM,
  EXPECT_RACE,
  VERBOSITY,
  STACK_TRACE,
  FLUSH_STATE,
  LAST_EVENT
};


static const char *kEventNames[] = {
  "NOOP",
  "READ",
  "WRITE",
  "READER_LOCK",
  "WRITER_LOCK",
  "UNLOCK",
  "UNLOCK_OR_INIT",
  "LOCK_CREATE",
  "LOCK_DESTROY",
  "BUS_LOCK_ACQUIRE",
  "BUS_LOCK_RELEASE",
  "THR_START",
  "THR_FIRST_INSN",
  "THR_END",
  "THR_JOIN_AFTER",
  "THR_STACK_TOP",
  "RTN_EXIT",
  "RTN_CALL",
  "SBLOCK_ENTER",
  "BBLOCK_ENTER",
  "SIGNAL",
  "WAIT_BEFORE",
  "WAIT_AFTER",
  "TWAIT_AFTER",
  "CYCLIC_BARRIER_INIT",
  "CYCLIC_BARRIER_WAIT_BEFORE",
  "CYCLIC_BARRIER_WAIT_AFTER",
  "PCQ_CREATE",
  "PCQ_DESTROY",
  "PCQ_PUT",
  "PCQ_GET",
  "SP_CHANGE",
  "STACK_MEM_NEW",
  "STACK_MEM_DIE",
  "MALLOC",
  "FREE",
  "PUBLISH_RANGE",
  "UNPUBLISH_RANGE",
  "HB_LOCK",
  "IGNORE_READS_BEG",
  "IGNORE_READS_END",
  "IGNORE_WRITES_BEG",
  "IGNORE_WRITES_END",
  "SET_THREAD_NAME",
  "SET_LOCK_NAME",
  "TRACE_MEM",
  "EXPECT_RACE",
  "VERBOSITY",
  "STACK_TRACE",
  "FLUSH_STATE",
  "LAST_EVENT"
};

// end. {{{1
#endif  // TS_EVENTS_H_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
