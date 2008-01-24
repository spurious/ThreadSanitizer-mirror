/*
  This file is part of Valgrind, a dynamic binary instrumentation
  framework.

  Copyright (C) 2008-2008 Google Inc
     opensource@google.com 

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

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
ANNOTATION(AnnotateExpectRace, void *mem, const char *description);

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
#define ANNOTATE_EXPECT_RACE(mem, description) \
            AnnotateExpectRace(__FILE__, __LINE__, mem, description)



#endif  // DYNAMIC_ANNOTATIONS_H__
