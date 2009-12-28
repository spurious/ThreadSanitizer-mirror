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

/* Author: Konstantin Serebryany <opensource@google.com>

 This file contains a set of unit tests for a data race detection tool.

 These tests can be compiled with pthreads (default) or
 with any other library that supports threads, locks, cond vars, etc.

*/
#ifdef WIN32
#error "Don't build this file on Windows!"
#endif

#include <fcntl.h>
#include <queue>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "old_test_suite.h"
#include "test_utils.h"

// test75: TN. Test for sem_post, sem_wait, sem_trywait. {{{1
namespace test75 {
int     GLOB = 0;
sem_t   sem[2];

void Poster() {
  GLOB = 1;
  sem_post(&sem[0]);
  sem_post(&sem[1]);
}

void Waiter() {
  sem_wait(&sem[0]);
  CHECK(GLOB==1);
}
void TryWaiter() {
  usleep(500000);
  sem_trywait(&sem[1]);
  CHECK(GLOB==1);
}

void Run() {
#ifndef NO_UNNAMED_SEM
  sem_init(&sem[0], 0, 0);
  sem_init(&sem[1], 0, 0);

  printf("test75: negative\n");
  {
    MyThreadArray t(Poster, Waiter);
    t.Start();
    t.Join();
  }
  GLOB = 2;
  {
    MyThreadArray t(Poster, TryWaiter);
    t.Start();
    t.Join();
  }
  printf("\tGLOB=%d\n", GLOB);

  sem_destroy(&sem[0]);
  sem_destroy(&sem[1]);
#endif // NO_UNNAMED_SEM
}
REGISTER_TEST(Run, 75)
}  // namespace test75


// test98: Synchronization via read/write (or send/recv). {{{1
namespace test98 {
// The synchronization here is done by a pair of read/write calls
// that create a happens-before arc. Same may be done with send/recv.
// Such synchronization is quite unusual in real programs
// (why would one synchronizae via a file or socket?), but
// quite possible in unittests where one threads runs for producer
// and one for consumer.
//
// A race detector has to create a happens-before arcs for
// {read,send}->{write,recv} even if the file descriptors are different.
//
int     GLOB = 0;
int fd_out = -1;
int fd_in  = -1;

void Writer() {
  usleep(1000);
  GLOB = 1;
  const char *str = "Hey there!\n";
  write(fd_out, str, strlen(str) + 1);
}

void Reader() {
  char buff[100];
  while (read(fd_in, buff, 100) == 0)
    sleep(1);
  printf("read: %s\n", buff);
  GLOB = 2;
}

void Run() {
  printf("test98: negative, synchronization via I/O\n");
  char in_name[100];
  char out_name[100];
  // we open two files, on for reading and one for writing,
  // but the files are actually the same (symlinked).
  sprintf(in_name,  "/tmp/racecheck_unittest_in.%d", getpid());
  sprintf(out_name, "/tmp/racecheck_unittest_out.%d", getpid());
  fd_out = creat(out_name, O_WRONLY | S_IRWXU);
  symlink(out_name, in_name);
  fd_in  = open(in_name, 0, O_RDONLY);
  CHECK(fd_out >= 0);
  CHECK(fd_in  >= 0);
  MyThreadArray t(Writer, Reader);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
  // cleanup
  close(fd_in);
  close(fd_out);
  unlink(in_name);
  unlink(out_name);
}
#ifndef __APPLE__
// Tsan for Mac OS is missing the unlink() syscall handler.
// TODO(glider): add the syscall handler to Valgrind.
REGISTER_TEST(Run, 98)
#endif
}  // namespace test98


// test106: TN. pthread_once. {{{1
namespace test106 {
int     *GLOB = NULL;
static pthread_once_t once = PTHREAD_ONCE_INIT;
void Init() {
  GLOB = new int;
  ANNOTATE_TRACE_MEMORY(GLOB);
  *GLOB = 777;
}

void Worker0() {
  pthread_once(&once, Init);
}
void Worker1() {
  usleep(100000);
  pthread_once(&once, Init);
  CHECK(*GLOB == 777);
}


void Run() {
  printf("test106: negative\n");
  MyThreadArray t(Worker0, Worker1, Worker1, Worker1);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", *GLOB);
}
REGISTER_TEST2(Run, 106, FEATURE)
}  // namespace test106


// test110: TP. Simple races with stack, global and heap objects. {{{1
namespace test110 {
int        GLOB = 0;
static int STATIC;

int       *STACK = 0;

int       *MALLOC;
int       *CALLOC;
int       *REALLOC;
int       *VALLOC;
int       *PVALLOC;
int       *MEMALIGN;
int       *POSIX_MEMALIGN;
int       *MMAP;

int       *NEW;
int       *NEW_ARR;

void Worker() {
  GLOB++;
  STATIC++;

  (*STACK)++;

  (*MALLOC)++;
  (*CALLOC)++;
  (*REALLOC)++;
  (*VALLOC)++;
  (*PVALLOC)++;
  (*MEMALIGN)++;
  (*POSIX_MEMALIGN)++;
  (*MMAP)++;

  (*NEW)++;
  (*NEW_ARR)++;
}
void Run() {
  printf("test110: positive (race on a stack object)\n");

  int x = 0;
  STACK = &x;

  MALLOC = (int*)malloc(sizeof(int));
  CALLOC = (int*)calloc(1, sizeof(int));
  REALLOC = (int*)realloc(NULL, sizeof(int));
  VALLOC = (int*)valloc(sizeof(int));
  PVALLOC = (int*)valloc(sizeof(int));  // TODO: pvalloc breaks helgrind.
  MEMALIGN = (int*)memalign(64, sizeof(int));
  CHECK(0 == posix_memalign((void**)&POSIX_MEMALIGN, 64, sizeof(int)));
  MMAP = (int*)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);

  NEW     = new int;
  NEW_ARR = new int[10];


  FAST_MODE_INIT(STACK);
  ANNOTATE_EXPECT_RACE(STACK, "real race on stack object");
  FAST_MODE_INIT(&GLOB);
  ANNOTATE_EXPECT_RACE(&GLOB, "real race on global object");
  FAST_MODE_INIT(&STATIC);
  ANNOTATE_EXPECT_RACE(&STATIC, "real race on a static global object");
  FAST_MODE_INIT(MALLOC);
  ANNOTATE_EXPECT_RACE(MALLOC, "real race on a malloc-ed object");
  FAST_MODE_INIT(CALLOC);
  ANNOTATE_EXPECT_RACE(CALLOC, "real race on a calloc-ed object");
  FAST_MODE_INIT(REALLOC);
  ANNOTATE_EXPECT_RACE(REALLOC, "real race on a realloc-ed object");
  FAST_MODE_INIT(VALLOC);
  ANNOTATE_EXPECT_RACE(VALLOC, "real race on a valloc-ed object");
  FAST_MODE_INIT(PVALLOC);
  ANNOTATE_EXPECT_RACE(PVALLOC, "real race on a pvalloc-ed object");
  FAST_MODE_INIT(MEMALIGN);
  ANNOTATE_EXPECT_RACE(MEMALIGN, "real race on a memalign-ed object");
  FAST_MODE_INIT(POSIX_MEMALIGN);
  ANNOTATE_EXPECT_RACE(POSIX_MEMALIGN, "real race on a posix_memalign-ed object");
  FAST_MODE_INIT(MMAP);
  ANNOTATE_EXPECT_RACE(MMAP, "real race on a mmap-ed object");

  FAST_MODE_INIT(NEW);
  ANNOTATE_EXPECT_RACE(NEW, "real race on a new-ed object");
  FAST_MODE_INIT(NEW_ARR);
  ANNOTATE_EXPECT_RACE(NEW_ARR, "real race on a new[]-ed object");

  MyThreadArray t(Worker, Worker, Worker);
  t.Start();
  t.Join();
  printf("\tSTACK=%d\n", *STACK);
  CHECK(GLOB <= 3);
  CHECK(STATIC <= 3);

  free(MALLOC);
  free(CALLOC);
  free(REALLOC);
  free(VALLOC);
  free(PVALLOC);
  free(MEMALIGN);
  free(POSIX_MEMALIGN);
  munmap(MMAP, sizeof(int));
  delete NEW;
  delete [] NEW_ARR;
}
REGISTER_TEST(Run, 110)
}  // namespace test110


// test115: TN. sem_open. {{{1
namespace    test115 {
int tid = 0;
Mutex mu;
const char *kSemName = "drt-test-sem";

int GLOB = 0;

sem_t *DoSemOpen() {
  // TODO: there is some race report inside sem_open
  // for which suppressions do not work... (???)
  ANNOTATE_IGNORE_WRITES_BEGIN();
  sem_t *sem = sem_open(kSemName, O_CREAT, 0600, 3);
  ANNOTATE_IGNORE_WRITES_END();
  return sem;
}

void Worker() {
  mu.Lock();
  int my_tid = tid++;
  mu.Unlock();

  if (my_tid == 0) {
    GLOB = 1;
  }

  // if the detector observes a happens-before arc between
  // sem_open and sem_wait, it will be silent.
  sem_t *sem = DoSemOpen();
  usleep(100000);
  CHECK(sem != SEM_FAILED);
  CHECK(sem_wait(sem) == 0);

  if (my_tid > 0) {
    CHECK(GLOB == 1);
  }
}

void Run() {
  printf("test115: stab (sem_open())\n");

  // just check that sem_open is not completely broken
  sem_unlink(kSemName);
  sem_t* sem = DoSemOpen();
  CHECK(sem != SEM_FAILED);
  CHECK(sem_wait(sem) == 0);
  sem_unlink(kSemName);

  // check that sem_open and sem_wait create a happens-before arc.
  MyThreadArray t(Worker, Worker, Worker);
  t.Start();
  t.Join();
  // clean up
  sem_unlink(kSemName);
}
#ifndef __APPLE__
/* This test is disabled for Darwin because of the tricky implementation of
 * sem_open on that platform: subsequent attempts to open an existing semafore
 * create new ones. */
REGISTER_TEST(Run, 115)
#endif
}  // namespace test115


// test122 TP: Simple test with RWLock {{{1
namespace  test122 {
int     VAR1 = 0;
int     VAR2 = 0;
RWLock mu;

void WriteWhileHoldingReaderLock(int *p) {
  usleep(100000);
  ReaderLockScoped lock(&mu);  // Reader lock for writing. -- bug.
  (*p)++;
}

void CorrectWrite(int *p) {
  WriterLockScoped lock(&mu);
  (*p)++;
}

void Thread1() { WriteWhileHoldingReaderLock(&VAR1); }
void Thread2() { CorrectWrite(&VAR1); }
void Thread3() { CorrectWrite(&VAR2); }
void Thread4() { WriteWhileHoldingReaderLock(&VAR2); }


void Run() {
  printf("test122: positive (rw-lock)\n");
  VAR1 = 0;
  VAR2 = 0;
  ANNOTATE_TRACE_MEMORY(&VAR1);
  ANNOTATE_TRACE_MEMORY(&VAR2);
  if (!Tsan_PureHappensBefore()) {
    ANNOTATE_EXPECT_RACE_FOR_TSAN(&VAR1, "test122. TP. ReaderLock-ed while writing");
    ANNOTATE_EXPECT_RACE_FOR_TSAN(&VAR2, "test122. TP. ReaderLock-ed while writing");
  }
  MyThreadArray t(Thread1, Thread2, Thread3, Thread4);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 122)
}  // namespace test122


// test125 TN: Backwards lock (annotated). {{{1
namespace test125 {
// This test uses "Backwards mutex" locking protocol.
// We take a *reader* lock when writing to a per-thread data
// (GLOB[thread_num])  and we take a *writer* lock when we
// are reading from the entire array at once.
//
// Such locking protocol is not understood by ThreadSanitizer's
// hybrid state machine. So, you either have to use a pure-happens-before
// detector ("tsan --pure-happens-before") or apply pure happens-before mode
// to this particular lock by using ANNOTATE_PURE_HAPPENS_BEFORE_MUTEX(&mu).

const int n_threads = 3;
RWLock   mu;
int     GLOB[n_threads];

int adder_num; // updated atomically.

void Adder() {
  int my_num = AtomicIncrement(&adder_num, 1);

  ReaderLockScoped lock(&mu);
  GLOB[my_num]++;
}

void Aggregator() {
  int sum = 0;
  {
    WriterLockScoped lock(&mu);
    for (int i = 0; i < n_threads; i++) {
      sum += GLOB[i];
    }
  }
  printf("sum=%d\n", sum);
}

void Run() {
  printf("test125: negative\n");

  ANNOTATE_PURE_HAPPENS_BEFORE_MUTEX(&mu);

  // run Adders, then Aggregator
  {
    MyThreadArray t(Adder, Adder, Adder, Aggregator);
    t.Start();
    t.Join();
  }

  // Run Aggregator first.
  adder_num = 0;
  {
    MyThreadArray t(Aggregator, Adder, Adder, Adder);
    t.Start();
    t.Join();
  }

}
REGISTER_TEST(Run, 125)
}  // namespace test125


// test135 TN. mmap {{{1
namespace test135 {

void SubWorker() {
  const long SIZE = 65536;
  for (int i = 0; i < 32; i++) {
    int *ptr = (int*)mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    *ptr = 42;
    munmap(ptr, SIZE);
  }
}

void Worker() {
  MyThreadArray t(SubWorker, SubWorker, SubWorker, SubWorker);
  t.Start();
  t.Join();
}

void Run() {
  printf("test135: negative (mmap)\n");
  MyThreadArray t(Worker, Worker, Worker, Worker);
  t.Start();
  t.Join();
}
REGISTER_TEST(Run, 135)
}  // namespace test135

// test136. Unlock twice. {{{1
namespace test136 {
void Run() {
  printf("test136: unlock twice\n");
  pthread_mutexattr_t attr;
  CHECK(0 == pthread_mutexattr_init(&attr));
  CHECK(0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));

  pthread_mutex_t mu;
  CHECK(0 == pthread_mutex_init(&mu, &attr));
  CHECK(0 == pthread_mutex_lock(&mu));
  CHECK(0 == pthread_mutex_unlock(&mu));
  int ret_unlock = pthread_mutex_unlock(&mu);  // unlocking twice.
  int ret_destroy = pthread_mutex_destroy(&mu);
  printf("  pthread_mutex_unlock returned %d\n", ret_unlock);
  printf("  pthread_mutex_destroy returned %d\n", ret_destroy);

}

REGISTER_TEST(Run, 136)
}  // namespace test136


// test141 FP. unlink/fopen, rmdir/opendir. {{{1
namespace test141 {
int GLOB1 = 0,
    GLOB2 = 0;
char *dir_name = NULL,
     *filename = NULL;

void Waker1() {
  usleep(100000);
  GLOB1 = 1;  // Write
  // unlink deletes a file 'filename'
  // which exits spin-loop in Waiter1().
  printf("  Deleting file...\n");
  CHECK(unlink(filename) == 0);
}

void Waiter1() {
  FILE *tmp;
  while ((tmp = fopen(filename, "r")) != NULL) {
    fclose(tmp);
    usleep(10000);
  }
  printf("  ...file has been deleted\n");
  GLOB1 = 2;  // Write
}

void Waker2() {
  usleep(100000);
  GLOB2 = 1;  // Write
  // rmdir deletes a directory 'dir_name'
  // which exit spin-loop in Waker().
  printf("  Deleting directory...\n");
  CHECK(rmdir(dir_name) == 0);
}

void Waiter2() {
  DIR *tmp;
  while ((tmp = opendir(dir_name)) != NULL) {
    closedir(tmp);
    usleep(10000);
  }
  printf("  ...directory has been deleted\n");
  GLOB2 = 2;
}

void Run() {
  FAST_MODE_INIT(&GLOB1);
  FAST_MODE_INIT(&GLOB2);
  printf("test141: FP. unlink/fopen, rmdir/opendir.\n");

  dir_name = strdup("/tmp/tsan-XXXXXX");
  CHECK(mkdtemp(dir_name) != 0);

  filename = strdup((std::string() + dir_name + "/XXXXXX").c_str());
  const int fd = mkstemp(filename);
  CHECK(fd >= 0);
  close(fd);

  MyThreadArray mta1(Waker1, Waiter1);
  mta1.Start();
  mta1.Join();

  MyThreadArray mta2(Waker2, Waiter2);
  mta2.Start();
  mta2.Join();

  free(filename);
  filename = 0;
  free(dir_name);
  dir_name = 0;
}
REGISTER_TEST(Run, 141)
}  // namespace test141


// test156: Signals and malloc {{{1
namespace test156 {
// Regression test for
// http://code.google.com/p/data-race-test/issues/detail?id=13 .
// Make sure that locking events are handled in signal handlers.
int     GLOB = 0;
Mutex mu;

static void SignalHandler(int, siginfo_t*, void*) {
  mu.Lock();
  GLOB++;
  mu.Unlock();
}

static void EnableSigprof() {
  struct sigaction sa;
  sa.sa_sigaction = SignalHandler;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGPROF, &sa, NULL) != 0) {
    perror("sigaction");
    abort();
  }
  struct itimerval timer;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 1000000 / 10000;
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, 0) != 0) {
    perror("setitimer");
    abort();
  }
}

void Worker() {
  for (int i = 0; i < 1000000; i++) {
    void *x = malloc((i % 64) + 1);
    free (x);
  }
}

void Run() {
  printf("test156: signals and malloc\n");
  EnableSigprof();
  MyThreadArray t(Worker, Worker, Worker);
  t.Start();
  t.Join();
  printf("\tGLOB=%d\n", GLOB);
}
REGISTER_TEST(Run, 156)
}  // namespace test156
