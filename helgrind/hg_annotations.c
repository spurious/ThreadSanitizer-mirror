/*
   This file is part of Helgrind, a Valgrind tool for detecting errors
   in threaded programs.

   Copyright 2008 Google Inc.

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

*/

/** 
  @file hg_annotations.h
  Author: Konstantin Serebryany <opensource@google.com> 

   RUNS ON SIMULATED CPU.
   Interceptors for Annotate* functions, so that user can annotate his/her source 
   so that hg_main can see significant thread events.

   See also dynamic_annotations.h.
 */

#include "pub_tool_basics.h"
#include "valgrind.h"
#include "helgrind.h"
#include <stdio.h>
#include <assert.h>

// Do a client request.  This is a macro rather than a function 
// so as to avoid having an extra function in the stack trace.
// TODO: merge with hg_intercepts.c

#define DO_CREQ_v_W(_creqF, _ty1F,_arg1F)                \
   do {                                                  \
      Word _unused_res, _arg1;                           \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1, 0,0,0,0);        \
   } while (0)

#define DO_CREQ_v_WW(_creqF, _ty1F,_arg1F, _ty2F,_arg2F) \
   do {                                                  \
      Word _unused_res, _arg1, _arg2;                    \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1,_arg2,0,0,0);     \
   } while (0)

#define DO_CREQ_v_WWW(_creqF, _ty1F,_arg1F,              \
		      _ty2F,_arg2F, _ty3F, _arg3F)       \
   do {                                                  \
      Word _unused_res, _arg1, _arg2, _arg3;             \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      assert(sizeof(_ty3F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      _arg3 = (Word)(_arg3F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1,_arg2,_arg3,0,0); \
   } while (0)

#define DO_CREQ_v_WWWW(_creqF, _ty1F,_arg1F, _ty2F,_arg2F,\
		      _ty3F,_arg3F, _ty4F, _arg4F)       \
   do {                                                  \
      Word _unused_res, _arg1, _arg2, _arg3, _arg4;      \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      assert(sizeof(_ty3F) == sizeof(Word));             \
      assert(sizeof(_ty4F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      _arg3 = (Word)(_arg3F);                            \
      _arg4 = (Word)(_arg4F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                              (_creqF),                  \
                             _arg1,_arg2,_arg3,_arg4,0); \
   } while (0)



#define TRACE_ANN_FNS 0




#define ANN_FUNC(ret_ty, f, args...) \
    ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args); \
    ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args)

#define ANN_TRACE(args...) \
    do{\
      if(TRACE_ANN_FNS){\
        int tid = VALGRIND_HG_THREAD_ID;\
        int sid = VALGRIND_HG_SEGMENT_ID;\
        fprintf(stderr, args);\
        if(tid != 999999 && sid != 999999) fflush(stderr);\
      }\
    }while(0)


ANN_FUNC(void, AnnotateRWLockCreate, const char *file, int line, void *lock)
{
  const char *name = "AnnotateRWLockCreate";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, lock, file, line);
  DO_CREQ_v_WW(_VG_USERREQ__HG_PTHREAD_RWLOCK_INIT_POST, void*, lock, long, 0 /*non recur*/);
}

ANN_FUNC(void, AnnotateRWLockDestroy, const char *file, int line, void *lock)
{
  const char *name = "AnnotateRWLockDestroy";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, lock, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PTHREAD_RWLOCK_DESTROY_PRE, void*, lock);
}


ANN_FUNC(void, AnnotateRWLockAcquired, const char *file, int line, void *lock, int is_w)
{
  const char *name = "AnnotateRWLockAcquired";
  ANN_TRACE("--#%d %s[%p] rw=%d %s:%d\n", tid, name, lock, is_w, file, line);
  DO_CREQ_v_WW(_VG_USERREQ__HG_PTHREAD_RWLOCK_LOCK_POST,  void*,lock,long, (long)is_w);
}

ANN_FUNC(void, AnnotateRWLockReleased, const char *file, int line, void *lock, int is_w)
{
  const char *name = "AnnotateRWLockReleased";
  ANN_TRACE("--#%d %s[%p] rw=%d %s:%d\n", tid, name, lock, is_w, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PTHREAD_RWLOCK_UNLOCK_PRE, void*, lock);
}

ANN_FUNC(void, AnnotateCondVarWait, const char *file, int line, void *cv, void *lock)
{
  const char *name = "AnnotateCondVarWait";
  ANN_TRACE("--#%d %s[%p|%p] %s:%d\n", tid, name, cv, lock, file, line);
  DO_CREQ_v_WW(_VG_USERREQ__HG_PTHREAD_COND_WAIT_POST, void*, cv, void *, lock);
}

ANN_FUNC(void, AnnotateCondVarSignal, const char *file, int line, void *cv)
{
  const char *name = "AnnotateCondVarSignal";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, cv, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PTHREAD_COND_SIGNAL_PRE, void*,cv);
}

ANN_FUNC(void, AnnotateCondVarSignalAll, const char *file, int line, void *cv)
{
  const char *name = "AnnotateCondVarSignalAll";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, cv, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PTHREAD_COND_BROADCAST_PRE, void*,cv);
}


ANN_FUNC(void, AnnotatePCQCreate, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQCreate";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PCQ_CREATE,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQDestroy, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQDestroy";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PCQ_DESTROY,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQPut, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQPut";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PCQ_PUT,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQGet, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQGet";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_PCQ_GET,   void*,pcq);
}

ANN_FUNC(void, AnnotateExpectRace, const char *file, int line, void *mem, char *description)
{
  const char *name = "AnnotateExpectRace";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_WWWW(_VG_USERREQ__HG_EXPECT_RACE, void*,mem, char*,description, char*, file, long, (long)line);
}

ANN_FUNC(void, AnnotateBenignRace, const char *file, int line, void *mem, char *description)
{
  const char *name = "AnnotateBenignRace";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_WWWW(VG_USERREQ__HG_BENIGN_RACE, void*,mem, char*,description, char*, file, long, (long)line);
}

ANN_FUNC(void, AnnotateNewMemory, char *file, int line, void *mem, long size)
{
  const char *name = "AnnotateNewMemory";
  ANN_TRACE("--#%d %s[%p,%d] %s:%d\n", tid, name, mem, (int)size, file, line);
  VALGRIND_HG_CLEAN_MEMORY(mem, size);
}

ANN_FUNC(void, AnnotateIgnoreReadsBegin, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreReadsBegin";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_IGNORE_READS_BEGIN,   void*, mu);
}

ANN_FUNC(void, AnnotateIgnoreReadsEnd, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreReadsEnd";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_IGNORE_READS_END,   void*, mu);
}

ANN_FUNC(void, AnnotateMutexIsUsedAsCondVar, char *file, int line, void *mu)
{
  const char *name = "AnnotateMutexIsUsedAsCondVar";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_MUTEX_IS_USED_AS_CONDVAR,   void*, mu);
}


ANN_FUNC(void, AnnotateNoOp, char *file, int line, void *mem)
{
  const char *name = "AnnotateNoOp";
  ANN_TRACE("--#%d/%d %s[%p] %s:%d\n", tid, sid, name, mem, file, line);
}


ANN_FUNC(void, AnnotateTraceMemory, char *file, int line, void *mem)
{
  const char *name = "AnnotateTraceMemory";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_W(_VG_USERREQ__HG_TRACE_MEM,   void*, mem);
}

#undef TRACE_ANN_FNS 
#define TRACE_ANN_FNS 1
