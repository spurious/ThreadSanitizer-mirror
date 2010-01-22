#include <gtest/gtest.h>

#include "test_utils.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace RaceReportDemoTest {  // {{{1
Mutex mu1;  // This Mutex guards var.
Mutex mu2;  // This Mutex is not related to var.
int   var;  // GUARDED_BY(mu1)

void Thread1() {  // Runs in thread named 'test-thread-1'.
  MutexLock lock(&mu1);  // Correct Mutex.
  var = 1;
}

void Thread2() {  // Runs in thread named 'test-thread-2'.
  MutexLock lock(&mu2);  // Wrong Mutex.
  var = 2;
}

TEST(DemoTests, RaceReportDemoTest) {
  ANNOTATE_TRACE_MEMORY(&var);
  var = 0;
  MyThread t1(Thread1, NULL, "test-thread-1");
  MyThread t2(Thread2, NULL, "test-thread-2");
  t1.Start();
  t2.Start();
  t1.Join();
  t2.Join();
}
}  // namespace RaceReportDemoTest
