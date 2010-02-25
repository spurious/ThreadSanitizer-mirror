/*
  This file is part of Valgrind, a dynamic binary instrumentation
  framework.

  Copyright (C) 2008-2008 Google Inc
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

/* Author: Timur Iskhodzhanov <opensource@google.com>

 This file contains a set of Windows-specific unit tests for
 a data race detection tool.
*/

#include <gtest/gtest.h>
#include "test_utils.h"

void DummyWorker() {
}

void LongWorker() {
  Sleep(1);
  volatile int i = 1 << 20;
  while(i--);
}

// Just spawn few threads with different stack sizes.
TEST(NegativeTests, WindowsThreadStackSizeTest) {
  int sizes[3] = {1 << 19, 1 << 21, 1 << 22};
  for (int i = 0; i < 3; i++) {
    HANDLE t = ::CreateThread(0, sizes[i],
                             (LPTHREAD_START_ROUTINE)DummyWorker, 0, 0, 0);
    CHECK(t > 0);
    ::WaitForSingleObject(t, INFINITE);
  }
}

// Just spawn few threads with different stack sizes.
TEST(NegativeTests, WindowsJoinWithTimeout) {
  HANDLE t = ::CreateThread(0, 0,
                            (LPTHREAD_START_ROUTINE)LongWorker, 0, 0, 0);
  CHECK(t > 0);
  CHECK(WAIT_TIMEOUT == ::WaitForSingleObject(t, 1));
  CHECK(WAIT_OBJECT_0 == ::WaitForSingleObject(t, INFINITE));
}

namespace RegisterWaitForSingleObjectTest {  // {{{1
StealthNotification *n = NULL;
HANDLE monitored_object = NULL;

void SignalStealthNotification() {
  n->wait();
  SetEvent(monitored_object);
}

void CALLBACK DoneWaiting(void *param, BOOLEAN timed_out) {
  int *i = (int*)param;
  (*i)++;
}

TEST(NegativeTests, DISABLED_WindowsRegisterWaitForSingleObjectTest) {  // {{{1
  // These are very tricky false positive found while testing Chromium.
  //
  // Report #1:
  //   Everything after UnregisterWaitEx(*, INVALID_HANDLE_VALUE) happens-after
  //   execution of DoneWaiting callback. Currently, we don't catch this h-b.
  //
  // Report #2:
  //   The callback thread is re-used between Registet/Unregister/Register calls
  //   so we miss h-b between "int *obj = ..." and DoneWaiting on the second
  //   iteration.
  for (int i = 0; i < 2; i++) {
    n = new StealthNotification();
    int *obj = new int(0);
    HANDLE wait_object = NULL;

    monitored_object = ::CreateEvent(NULL, false, false, NULL);
    printf("monitored_object = %p\n", monitored_object);
    MyThread mt(SignalStealthNotification);
    mt.Start();
    ANNOTATE_TRACE_MEMORY(obj);
    CHECK(0 != ::RegisterWaitForSingleObject(&wait_object, monitored_object,
                                             DoneWaiting, obj, INFINITE,
                                             WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE));
    printf("wait_object      = %p\n", wait_object);
    n->signal();
    mt.Join();
    Sleep(1000);
    CHECK(0 != ::UnregisterWaitEx(wait_object, INVALID_HANDLE_VALUE));
    (*obj)++;
    CHECK(*obj == 2);
    CloseHandle(monitored_object);
    delete n;
    delete obj;
  }
}
}

namespace QueueUserWorkItemTest {
DWORD CALLBACK Callback(void *param) {
  int *ptr = (int*)param;
  (*ptr)++;
  delete ptr;
  return 0;
}

TEST(NegativeTests, DISABLED_WindowsQueueUserWorkItemTest) {
  // False positive:
  //   The callback thread is allocated from a thread pool and can be re-used.
  //   As a result, we may miss h-b between "int *obj = ..." and Callback execution.
  for (int i = 0; i < 1024; i++) {
    int *obj = new int(0);
    ANNOTATE_TRACE_MEMORY(obj);
    CHECK(QueueUserWorkItem(Callback, obj, i % 2 ? WT_EXECUTELONGFUNCTION : 0));
  }
}
}

namespace WindowsSRWLockTest {  // {{{1
SRWLOCK SRWLock;
int *obj;

void Reader() {
  AcquireSRWLockShared(&SRWLock);
  CHECK(*obj <= 2 && *obj >= 0);
  ReleaseSRWLockShared(&SRWLock);
}

void Writer() {
  AcquireSRWLockExclusive(&SRWLock);
  (*obj)++;
  ReleaseSRWLockExclusive(&SRWLock);
}

#if 0  // TODO(kcc): this doesn't seem to work.
void TryReader() {
  if (TryAcquireSRWLockShared(&SRWLock)) {
    CHECK(*obj <= 2 && *obj >= 0);
    ReleaseSRWLockShared(&SRWLock);
  }
}

void TryWriter() {
  if (TryAcquireSRWLockExclusive(&SRWLock)) {
    (*obj)++;
    ReleaseSRWLockExclusive(&SRWLock);
  }
}
#endif

TEST(NegativeTests, DISABLED_WindowsSRWLockTest) {
  InitializeSRWLock(&SRWLock);
  obj = new int(0);
  ANNOTATE_TRACE_MEMORY(obj);
  MyThreadArray t(Reader, Writer, Reader, Writer);
  t.Start();
  t.Join();
  AcquireSRWLockShared(&SRWLock);
  ReleaseSRWLockShared(&SRWLock);
  CHECK(*obj == 2);
  delete obj;
}
}  // namespace

namespace WindowsConditionVariableSRWTest {  // {{{1
SRWLOCK SRWLock;
CONDITION_VARIABLE cv;
bool cond;
int *obj;

StealthNotification n;

void WaiterSRW() {
  *obj = 1;
  n.wait();
  AcquireSRWLockExclusive(&SRWLock);
  cond = true;
  WakeConditionVariable(&cv);
  ReleaseSRWLockExclusive(&SRWLock);
}

void WakerSRW() {
  AcquireSRWLockExclusive(&SRWLock);
  n.signal();
  while (!cond) {
    SleepConditionVariableSRW(&cv, &SRWLock, 10, 0);
  }
  ReleaseSRWLockExclusive(&SRWLock);
  CHECK(*obj == 1);
  *obj = 2;
}

TEST(NegativeTests, DISABLED_WindowsConditionVariableSRWTest) {
  InitializeSRWLock(&SRWLock);
  InitializeConditionVariable(&cv);
  obj = new int(0);
  cond = false;
  ANNOTATE_TRACE_MEMORY(obj);
  MyThreadArray t(WaiterSRW, WakerSRW);
  t.Start();
  t.Join();
  CHECK(*obj == 2);
  delete obj;
}

}  // namespace
// End {{{1
 // vim:shiftwidth=2:softtabstop=2:expandtab:foldmethod=marker
