// Copyright 2010 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#ifndef STACK_INFO_H
#define STACK_INFO_H

#include <pthread.h>
#include <stddef.h>

bool guess_thread_stack_info(void *(*start_routine)(void*),
                             void *arg,
                             /* out */ void **stack_top,
                             /* out */ size_t *stack_size);

#endif // STACK_INFO_H
