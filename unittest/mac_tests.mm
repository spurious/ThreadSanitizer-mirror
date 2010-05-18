#include <fenv.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/mman.h>
#import <Foundation/Foundation.h>

#ifndef OS_darwin
#error "This file should be built on Darwin only."
#endif

class Task {
 public:
  Task()  {};
  void Run() { printf("Inside Task::Run()\n"); }
};

@interface TaskOperation : NSOperation {
 @private
  Task *task_;
}

+ (id)taskOperationWithTask:(Task*)task;

- (id)initWithTask:(Task*)task;

@end  // @interface TaskOperation

@implementation TaskOperation

+ (id)taskOperationWithTask:(Task*)task {
  return [[TaskOperation alloc] initWithTask:task];
}

- (id)init {
  return [self initWithTask:NULL];
}

- (id)initWithTask:(Task*)task {
  if ((self = [super init])) {
    task_ = task;
  }
  return self;
}

- (void)main {
  if (!task_) {
    return;
  }

  task_->Run();
  delete task_;
  task_ = NULL;
}

- (void)dealloc {
  if (task_) delete task_;
  [super dealloc];
}

@end  // @implementation TaskOperation

namespace MacTests {
// Regression test for https://bugs.kde.org/show_bug.cgi?id=216837.
TEST(MacTests, WqthreadRegressionTest) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  Task *task = new Task();
  NSOperationQueue *queue = [[NSOperationQueue alloc] init];
  [queue addOperation:[TaskOperation taskOperationWithTask:task]];

  sleep(1);  // wait for the worker thread to start.
  // If the bug is still there, ThreadSanitizer should crash after starting
  // the worker thread.
  [pool release];
}

// Regression test for a bug with passing a 32-bit value instead of a 64-bit
// as the last parameter to mmap().
TEST(MacTests, ShmMmapRegressionTest) {
  int md;
  void *virt_addr;
  md = shm_open("apple.shm.notification_center", 0, 0);
  virt_addr = mmap(0, 4096, 1, 1, md, 0);
  if (virt_addr == (void*)-1) {
    FAIL() << "mmap returned -1";
  } else {
    munmap(virt_addr, 4096);
    shm_unlink("apple.shm.notification_center");
  }
}

TEST(MacTests, DISABLED_FegetenvTest) {
  fenv_t tmp;
  if (fegetenv(&tmp) != 0)
    FAIL() << "fegetenv failed";
}

}  // namespace MacTests
