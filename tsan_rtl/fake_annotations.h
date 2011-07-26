// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)
//
// Fake annotations to use with ThreadSanitizer runtime library.

#ifndef FAKE_ANNOTATIONS_H_
#define FAKE_ANNOTATIONS_H_

extern "C"
void FakeAnnotatePrintStackTrace(const char *file, int line);

#define ANNOTATE_PRINT_STACK_TRACE() \
    FakeAnnotatePrintStackTrace(__FILE__, __LINE__)

#endif  // FAKE_ANNOTATIONS_H_
