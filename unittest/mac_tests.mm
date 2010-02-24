#include <gtest/gtest.h>
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
TEST(MacTests, DISABLED_WqthreadRegressionTest) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  Task *task = new Task();
  NSOperationQueue *queue = [[NSOperationQueue alloc] init];
  [queue addOperation:[TaskOperation taskOperationWithTask:task]];

  sleep(1);  // wait for the worker thread to start.
  // If the bug is still there, ThreadSanitizer should crash after starting
  // the worker thread.
  [pool release];
}

}  // namespace
