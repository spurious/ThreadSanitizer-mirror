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

namespace RegisterWaitForSingleObjectTest {
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

TEST(NegativeTests, DISABLED_RegisterWaitForSingleObjectTest) {
  // These are very tricky false positive found while testing Chromium.
  //
  // Report #1:
  //   Everything after UnregisterWaitEx(*, INVALID_HANDLE_VALUE) happens-after
  //   execution of DoneWaiting callback. Currently, we don't catch this h-b.
  //
  // Report #2:
  //   The callback thread is re-used between Registet/Unregister/Register calls
  //   so we miss h-b between "int *INT = ..." and DoneWaiting on the second
  //   iteration.
  for (int i = 0; i < 2; i++) {
    n = new StealthNotification();
    int *INT = new int(0);
    HANDLE wait_object = NULL;

    monitored_object = ::CreateEvent(NULL, false, false, NULL);
    printf("monitored_object = %p\n", monitored_object);
    MyThread mt(SignalStealthNotification);
    mt.Start();
    CHECK(0 != ::RegisterWaitForSingleObject(&wait_object, monitored_object,
                                             DoneWaiting, INT, INFINITE,
                                             WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE));
    printf("wait_object      = %p\n", wait_object);
    n->signal();
    mt.Join();
    Sleep(1000);
    CHECK(0 != ::UnregisterWaitEx(wait_object, INVALID_HANDLE_VALUE));
    (*INT)++;
    CHECK(*INT == 2);
    CloseHandle(monitored_object);
    delete n;
    delete INT;
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

TEST(NegativeTests, DISABLED_QueueUserWorkItemTest) {
  // False positive:
  //   The callback thread is allocated from a thread pool and can be re-used.
  //   As a result, we may miss h-b between "int *INT = ..." and Callback execution.
  for (int i = 0; i < 1024; i++) {
    int *INT = new int(0);
    CHECK(QueueUserWorkItem(Callback, INT, i % 2 ? WT_EXECUTELONGFUNCTION : 0));
  }
}
}
