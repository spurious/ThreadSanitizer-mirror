// Copyright 2008, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,           
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY           
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

// Author: Konstantin Serebryany <opensource@google.com> 
//
// This file defines dynamic annotations for use with dynamic analysis 
// tool like valgrind, PIN, etc. 
//
// Dynamic annotation is a source code annotation which affects 
// the generated code (i.e. not a comment). 
// Each such annotation is attached to a particular
// instruction and/or to a particular object (address) in the program.
//
// The annotations that should be used by users are macros 
// (e.g. ANNOTATE_NEW_MEMORY). 
//
// Actual implementation of these macros may differ depending on the 
// dynamic analysis tool beeing used. 
//
// Right now this file supports the following dynamic analysis tools: 
// - None (DYNAMIC_ANNOTATIONS is not defined). 
//    Macros are defined empty. 
// - Helgrind (DYNAMIC_ANNOTATIONS is defined). 
//    Macros are defined as calls to non-inlinable empty functions 
//    that are intercepted by helgrind (starting from version TODO). 
//
//
// To link your program with annotations enabled, add a new file 
// with the following lines to your program: 
//    #define DYNAMIC_ANNOTATIONS
//    #define DYNAMIC_ANNOTATIONS_HERE
//    #include "dynamic_annotations.h"  
//
//
// FIXME: using __attribute__((noinline)) might be simpler, 
// but it is less portable. 
//
#ifndef DYNAMIC_ANNOTATIONS_H__
#define DYNAMIC_ANNOTATIONS_H__


#ifdef DYNAMIC_ANNOTATIONS
// Annotations are enabled. 
# ifdef DYNAMIC_ANNOTATIONS_HERE
// Actually define the annotations as functions with empty body. 
#  define ANNOTATION(name, arglist...) \
    extern "C" void name (const char *file, int line, arglist) {}
# else
// Just declare the functions. 
#  define ANNOTATION(name, arglist...) \
    extern "C" void name (const char *file, int line, arglist); 
# endif
#else  // !DYNAMIC_ANNOTATIONS
// Annotations are disabled. Define an empty inlinable function. 
# define ANNOTATION(name, arglist...) \
    static inline void name (const char *file, int line, arglist) {}
#endif


ANNOTATION(AnnotateRWLockCreate, void *lock);
ANNOTATION(AnnotateRWLockDestroy, void *lock);
ANNOTATION(AnnotateRWLockAcquired, void *lock, long is_w);
ANNOTATION(AnnotateRWLockReleased, void *lock, long is_w);
ANNOTATION(AnnotateCondVarWait,     void *cv, void *lock);
ANNOTATION(AnnotateCondVarSignal,   void *cv);
ANNOTATION(AnnotateCondVarSignalAll,void *cv);
ANNOTATION(AnnotatePCQPut,  void *uniq_id);
ANNOTATION(AnnotatePCQGet,  void *uniq_id);
ANNOTATION(AnnotateNewMemory, void *mem, long size);
ANNOTATION(AnnotateExpectRace, void *mem);

/// Insert right after the lock is created. 
#define ANNOTATE_RWLOCK_CREATE(lock) \
           AnnotateRWLockCreate(__FILE__, __LINE__, lock)

/// Insert right before the lock is destroyed. 
#define ANNOTATE_RWLOCK_DESTROY(lock) \
           AnnotateRWLockDestroy(__FILE__, __LINE__, lock)

/// Insert right after the point were 'lock' is acquired.
/// Set is_w=1 for write lock, is_w=0 for reader lock. 
#define ANNOTATE_RWLOCK_ACQUIRED(lock, is_w) \
            AnnotateRWLockAcquired(__FILE__, __LINE__, lock, is_w)

/// Insert right before the point where 'lock' is released. 
#define ANNOTATE_RWLOCK_RELEASED(lock, is_w) \
            AnnotateRWLockReleased(__FILE__, __LINE__, lock, is_w)

/// Insert right after the point where wait has succeeded. 
/// 'lock' can be the same address as 'cv'. 
#define ANNOTATE_CONDVAR_WAIT(cv, lock) \
            AnnotateCondVarWait(__FILE__, __LINE__, cv, lock)
/// Insert right before the signal. 
#define ANNOTATE_CONDVAR_SIGNAL(cv) \
            AnnotateCondVarSignal(__FILE__, __LINE__, cv)
/// Same as ANNOTATE_CONDVAR_SIGNAL.
#define ANNOTATE_CONDVAR_SIGNAL_ALL(cv) \
            AnnotateCondVarSignalAll(__FILE__, __LINE__, cv)

/// Insert right before putting element into the queue (in Put()). 
/// 'uniq_id' should be put into queue. 
#define ANNOTATE_PCQ_PUT(uniq_id) \
            AnnotatePCQPut(__FILE__, __LINE__, \
                           reinterpret_cast<void*>(uniq_id))

/// Insert rigth after getting element from the queue (in Get()). 
#define ANNOTATE_PCQ_GET(uniq_id) \
            AnnotatePCQGet(__FILE__, __LINE__, \
                           reinterpret_cast<void*>(uniq_id))

/// Insert at the very end of malloc-like function. 
#define ANNOTATE_NEW_MEMORY(mem, size) \
            AnnotateNewMemory(__FILE__, __LINE__, mem, size)

/// Insert at the beginning of a unit test. 
#define ANNOTATE_EXPECT_RACE(mem) \
            AnnotateExpectRace(__FILE__, __LINE__, mem)



#endif  // DYNAMIC_ANNOTATIONS_H__
