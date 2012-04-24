#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include "../tsan/tsan_interface_atomic.h"

struct Cache {
  int x;
};

Cache g_cache;

Cache *CreateCache() {
  g_cache.x = rand();
  return &g_cache;
}

__tsan_atomic64 queue;

void *Thread1(void *x) {
  static Cache *c = CreateCache();
  __tsan_atomic64_store(&queue, (__tsan_atomic64)c, __tsan_memory_order_relaxed);
  return 0;
}

void *Thread2(void *x) {
  Cache *c = 0;
  for (;;) {
    c = (Cache*)__tsan_atomic64_load(&queue, __tsan_memory_order_relaxed);
    if (c)
      break;
    sched_yield();
  }
  if (c->x >= RAND_MAX)
    exit(1);
  return 0;
}

int main() {
  pthread_t t[2];
  pthread_create(&t[0], 0, Thread1, 0);
  pthread_create(&t[1], 0, Thread2, 0);
  pthread_join(t[0], 0);
  pthread_join(t[1], 0);
}

// CHECK: WARNING: ThreadSanitizer: data race
