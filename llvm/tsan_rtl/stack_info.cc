// Copyright 2010 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#include "stack_info.h"

#include <stdint.h>

#ifdef STACKINFO_STANDALONE
#include <stdio.h>
#define Printf printf
#endif

static const uintptr_t kPageSize = 4096;
static const uintptr_t kPageMask = ~(kPageSize-1);

bool guess_thread_stack_info(void *(*start_routine)(void*),
                             void *arg,
                             /* out */void **stack_top,
                             /* out */size_t *stack_size) {
  // The address space for the stack and TLS is normally allocated by a single
  // mmap2 system call, which should be passed to ThreadSanitizer in order to
  // avoid false positives when the stack is reused by another thread.
  // The region also contains a stack guard, which is 
  // Because it's hard to intercept a syscall, we try to get the start of the
  // allocated region and its size from *(pthread_self()), which appears to be
  // a pointer to struct pthread in Linux (see glibc-v.v.v/nptl/descr.h)
  //
  // struct pthread is organized as follows:
  //
  // struct pthread {
  //   ...
  //   void *(*start_routine) (void *);
  //   void *arg;
  //   ...
  //   void *stackblock;
  //   size_t stackblock_size;
  //   ...
  // }
  //
  // The omitted fields may have different sizes depending on the compile-time
  // options, but we assume that |start_routine|, |arg|, |stackblock| and
  // |stackblock_size| are word-aligned and reside in the first 1K of the
  // struct pthread.

  bool result = true;
  // Stack is page-aligned.
  uintptr_t local_stack_top = (uintptr_t)(&result) & kPageMask;
  if (local_stack_top != (uintptr_t)&result) local_stack_top += kPageSize;

  Printf("&result=%p, &local_stack_top=%p, local_stack_top=%p\n", &result, &local_stack_top,
         (void*)local_stack_top);

  uintptr_t *pt_it = (uintptr_t*) pthread_self();
  Printf("pt_it: %p\n", pt_it);

  // Iff start_routine is NULL, skip the sanity check.
  bool sanity_rtn_arg = (start_routine == NULL);
  const int kTraversalSize = 256;
  for (int i = 0; i < kTraversalSize; i++) {

    // Sanity check: |start_routine| and |arg| occur before the stack info.
    if (start_routine && (pt_it[i] == uintptr_t(start_routine)) &&
        (i+1 < kTraversalSize) && (pt_it[i+1] == (uintptr_t)arg)) {
      sanity_rtn_arg = true;
    }

    // Stack top must be page aligned.
    if ((pt_it[i] & kPageMask) != pt_it[i]) continue;

    if (local_stack_top -pt_it[i] + 16384 - pt_it[i+1] == 0) {
      Printf("stack_top: %p, size: %d\n", (void*)pt_it[i], pt_it[i+1]);
//      break;
    } else {
      if (pt_it[i]) {
        Printf("pt_it[%d]=%p\n", i, (void*)pt_it[i]);
//        Printf("diff: %d\n", local_stack_top - pt_it[i] - pt_it[i+1]);
      }
    }
  }
  return result;
}

#ifdef STACKINFO_STANDALONE
// Compile a stand-alone executable for testing guess_thread_stack_info().

__thread int local;

void *worker(void *unused) {
  void *stack_top;
  size_t stack_size;
  if (guess_thread_stack_info(worker, unused, &stack_top, &stack_size)) {
    printf("stack top: %p\nstack size: %d\n", stack_top, stack_size);
  }
  printf("&local: %p\n", &local);
}

int main() {
  pthread_t pt;
  pthread_create(&pt, NULL, worker, NULL);
  pthread_join(pt, NULL);
}
#endif
