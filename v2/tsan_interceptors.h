//===-- tsan_interceptors.cc ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Platform-independent code for interceptors.
// Do not put any platform-specific includes/ifdefs here.
//===----------------------------------------------------------------------===//
#ifdef __linux__
#define WRAP(name) name
#define ORIG(name) orig_##name
#else
# error "This platform is not supported"
#endif

