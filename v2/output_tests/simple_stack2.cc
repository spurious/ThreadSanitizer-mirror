#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int Global;

void __attribute__((noinline)) foo1() {
  Global = 42;
}

void __attribute__((noinline)) bar1() {
  volatile int tmp = 42;
  foo1();
}

void __attribute__((noinline)) foo2() {
  Global = 43;
}

void __attribute__((noinline)) bar2() {
  volatile int tmp = 42;
  foo2();
}

void *Thread1(void *x) {
  usleep(10000);
  bar1();
  return NULL;
}

int main() {
  pthread_t t;
  pthread_create(&t, NULL, Thread1, NULL);
  bar2();
  pthread_join(t, NULL);
}

// CHECK:      WARNING: ThreadSanitizer: data race
// CHECK-NEXT:   Write of size 4 at {{.*}} by thread 1:
// CHECK-NEXT:     #0 {{.*}}: foo1() simple_stack2.cc:8
// CHECK-NEXT:     #1 {{.*}}: bar1() simple_stack2.cc:14
// CHECK-NEXT:     #2 {{.*}}: Thread1(void*) simple_stack2.cc:28
// CHECK:        Previous Write of size 4 at {{.*}} by thread 0:
// CHECK-NEXT:     #0 {{.*}}: foo2() simple_stack2.cc:17
// CHECK-NEXT:     #1 {{.*}}: bar2() simple_stack2.cc:23
// CHECK-NEXT:     #2 {{.*}}: main simple_stack2.cc:35


