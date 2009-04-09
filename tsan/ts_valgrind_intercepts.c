/*
  This file is part of ThreadSanitizer, a dynamic data race detector 
  based on Valgrind.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com 
  Copyright (C) 2007-2008 OpenWorks LLP
      info@open-works.co.uk

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

// Author: Konstantin Serebryany.
// Parts of the code in this file are taken from Helgrind, 
// a data race detector written by Julian Seward. 

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>  // O_CREAT

#include "valgrind.h"
#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_redir.h"   

#include "ts_valgrind_client_requests.h"

#define TRACE_PTH_FNS 0
#define TRACE_ANN_FNS 0


//----------- Basic stuff --------------------------- {{{1

/* BEGIN tiresome boilerplate.  All this stuff is copied from
   memcheck/mc_replace_strmem.c.  If you update that, consider
   updating this too (and vice versa). */

/* --- Soname of the standard C library. --- */

#if defined(VGO_linux)
#  define  m_libc_soname     libcZdsoZa              // libc.so*
#elif defined(VGP_ppc32_aix5)
   /* AIX has both /usr/lib/libc.a and /usr/lib/libc_r.a. */
#  define  m_libc_soname     libcZaZdaZLshrZdoZR     // libc*.a(shr.o)
#elif defined(VGP_ppc64_aix5)
#  define  m_libc_soname     libcZaZdaZLshrZu64ZdoZR // libc*.a(shr_64.o)
#else
#  error "Unknown platform"
#endif



/* --- Sonames for Linux ELF linkers. --- */

#define  m_ld_linux_so_2         ldZhlinuxZdsoZd2           // ld-linux.so.2
#define  m_ld_linux_x86_64_so_2  ldZhlinuxZhx86Zh64ZdsoZd2  // ld-linux-x86-64.so.2
#define  m_ld64_so_1             ld64ZdsoZd1                // ld64.so.1
#define  m_ld_so_1               ldZdsoZd1                  // ld.so.1

/* END tiresome boilerplate */


static inline int VALGRIND_TS_THREAD_ID(void) {
  unsigned int _qzz_res;                                       
  VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 ,                      
                             TSREQ_GET_THREAD_ID,     
                             0, 0, 0, 0, 0);                   
  return _qzz_res;                                                    
}

static inline int  VALGRIND_TS_SEGMENT_ID(void) {
  unsigned int _qzz_res;                                       
  VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 ,                      
                             TSREQ_GET_SEGMENT_ID,     
                             0, 0, 0, 0, 0);                    
  return _qzz_res;                                                     
}



#define PTH_FUNC(ret_ty, f, args...) \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(libpthreadZdsoZd0,f)(args); \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(libpthreadZdsoZd0,f)(args)

#define NONE_FUNC(ret_ty, f, args...) \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args); \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args)

// libstdcZpZpZa = libstdc++
#define LIBSTDCXX_FUNC(ret_ty, f, args...) \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(libstdcZpZpZa,f)(args); \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(libstdcZpZpZa,f)(args)


// Do a client request.  This is a macro rather than a function 
// so as to avoid having an extra function in the stack trace.

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

#define DO_CREQ_W_WW(_resF, _creqF, _ty1F,_arg1F, _ty2F,_arg2F)	\
   do {                                                  \
      Word _res, _arg1, _arg2;                           \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_res, 2,                \
                                 (_creqF),               \
                                 _arg1,_arg2,0,0,0);     \
      _resF = _res;                                      \
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



#define DO_PthAPIerror(_fnnameF, _errF)                  \
   do {                                                  \
      char* _fnname = (char*)(_fnnameF);                 \
      long  _err    = (long)(int)(_errF);	         \
      char* _errstr = lame_strerror(_err);               \
      DO_CREQ_v_WWW(TSREQ_PTH_API_ERROR,       \
                    char*,_fnname,                       \
                    long,_err, char*,_errstr);           \
   } while (0)

static inline void IGNORE_ALL_BEGIN(void) {
   DO_CREQ_v_W(TSREQ_IGNORE_ALL_BEGIN,  void*, NULL);
}

static inline void IGNORE_ALL_END(void) {
   DO_CREQ_v_W(TSREQ_IGNORE_ALL_END,  void*, NULL);
}



//-------------- Wrapper for main() -------- {{{1
#define MAIN_WRAPPER_DECL \
 int I_WRAP_SONAME_FNNAME_ZU(NONE,main) (long argc, char **argv, char **env) 

MAIN_WRAPPER_DECL;
MAIN_WRAPPER_DECL {
  int ret; 
  OrigFn fn;
  VALGRIND_GET_ORIG_FN(fn);
  DO_CREQ_v_WW(TSREQ_MAIN_IN,  long, argc, char **, argv); 
  CALL_FN_W_WWW(ret, fn, argc, argv, env);
  DO_CREQ_v_W(TSREQ_MAIN_OUT,  void*, ret); 
  return ret; 
}

//-------------- MALLOC -------------------- {{{1

#define WRAP_MALLOC(soname, fnname) \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (SizeT n); \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (SizeT n) { \
    void* ret; \
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_W(ret, fn, n); \
    IGNORE_ALL_END(); \
    DO_CREQ_v_WW(TSREQ_MALLOC,  void*, ret, long, n); \
    return ret; \
  }

#define WRAP_CALLOC(soname, fnname) \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (SizeT n, SizeT c); \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (SizeT n, SizeT c) { \
    void* ret; \
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_WW(ret, fn, n, c); \
    IGNORE_ALL_END(); \
    DO_CREQ_v_WW(TSREQ_MALLOC,  void*, ret, long, n * c); \
    return ret; \
  }

#define WRAP_REALLOC(soname, fnname) \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr, SizeT n); \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr, SizeT n) { \
    void* ret; \
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_WW(ret, fn, ptr, n); \
    IGNORE_ALL_END(); \
    DO_CREQ_v_WW(TSREQ_MALLOC,  void*, ret, long, n); \
    return ret; \
  }

#define WRAP_POSIX_MEMALIGN(soname, fnname) \
  int I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void **ptr, long a, long size);\
  int I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void **ptr, long a, long size){\
    OrigFn fn;\
    int ret;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_WWW(ret, fn, ptr, a, size); \
    IGNORE_ALL_END(); \
    if (ret == 0) \
      DO_CREQ_v_WW(TSREQ_MALLOC,  void*, *ptr, long, size); \
    return ret; \
  }

#define WRAP_MMAP(soname, fnname) \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr, long size, long a, long b, long c, long d);\
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr, long size, long a, long b, long c, long d){\
    void* ret;\
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_6W(ret, fn, ptr, size, a, b, c, d); \
    IGNORE_ALL_END(); \
    DO_CREQ_v_WW(TSREQ_MALLOC,  void*, ret, long, size); \
    return ret; \
  }


WRAP_MALLOC(m_libc_soname, malloc);
WRAP_MALLOC(NONE, malloc);

WRAP_MALLOC(m_libc_soname, valloc);
WRAP_MALLOC(NONE, valloc);
WRAP_MALLOC(m_libc_soname, pvalloc);
WRAP_MALLOC(NONE, pvalloc);
WRAP_MALLOC(NONE, _Znam);
WRAP_MALLOC(NONE, _Znwm);
WRAP_MALLOC(NONE, _Znaj);
WRAP_MALLOC(NONE, _Znwj);

WRAP_CALLOC(m_libc_soname, calloc);
WRAP_CALLOC(NONE, calloc);

WRAP_REALLOC(m_libc_soname, realloc); // TODO: handle free inside realloc
WRAP_REALLOC(NONE, realloc); // TODO: handle free inside realloc
WRAP_REALLOC(m_libc_soname, memalign);
WRAP_REALLOC(NONE, memalign);
WRAP_POSIX_MEMALIGN(m_libc_soname, posix_memalign);
WRAP_POSIX_MEMALIGN(NONE, posix_memalign);

// TODO(timurrrr): handle munmap.
// Looks like munmap may be used to free page-sized subregions of memory
// returned my mmap. This could be nasty. Need investigation.
WRAP_MMAP(m_libc_soname, mmap);
WRAP_MMAP(NONE, mmap);

#define WRAP_FREE(soname, fnname) \
  void I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr); \
  void I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *ptr) { \
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    DO_CREQ_v_W(TSREQ_FREE,  void*, ptr); \
    IGNORE_ALL_BEGIN(); \
      CALL_FN_v_W(fn, ptr); \
    IGNORE_ALL_END(); \
  }

WRAP_FREE(m_libc_soname, free);

WRAP_FREE(NONE, free);
WRAP_FREE(NONE, _ZdlPv);
WRAP_FREE(NONE, _ZdaPv);


//-------------- PTHREADS -------------------- {{{1

#if 0
#define LD_FUNC(ret_ty, f, args...) \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(ldZhZa,f)(args); \
   ret_ty I_WRAP_SONAME_FNNAME_ZZ(ldZhZa,f)(args)


LD_FUNC(long, doZulookupZux, void * arg1, void * arg2, void * arg3,
        void * arg4, void * arg5, void * arg6, void * arg7, void * arg8,
        void * arg9, void * arg10, void * arg11)
{
   long result;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);   
   IGNORE_ALL_BEGIN();
   CALL_FN_W_11W(result, fn, arg1, arg2, arg3, arg4, arg5, arg6,
                    arg7, arg8, arg9, arg10, arg11);
   IGNORE_ALL_END();
   return result;
}

LD_FUNC(Word, ZaZudlZustartZa, void * arg)
{
   Word result;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   // why is this not called???
   tl_assert(0);
   IGNORE_ALL_BEGIN();
   CALL_FN_W_W(result, fn, arg);
   IGNORE_ALL_END();
   return result;
}

#endif 

/* A lame version of strerror which doesn't use the real libc
   strerror_r, since using the latter just generates endless more
   threading errors (glibc goes off and does tons of crap w.r.t.
   locales etc) */
static char* lame_strerror ( long err )
{   switch (err) {
      case EPERM:       return "EPERM: Operation not permitted";
      case ENOENT:      return "ENOENT: No such file or directory";
      case ESRCH:       return "ESRCH: No such process";
      case EINTR:       return "EINTR: Interrupted system call";
      case EBADF:       return "EBADF: Bad file number";
      case EAGAIN:      return "EAGAIN: Try again";
      case ENOMEM:      return "ENOMEM: Out of memory";
      case EACCES:      return "EACCES: Permission denied";
      case EFAULT:      return "EFAULT: Bad address";
      case EEXIST:      return "EEXIST: File exists";
      case EINVAL:      return "EINVAL: Invalid argument";
      case EMFILE:      return "EMFILE: Too many open files";
      case ENOSYS:      return "ENOSYS: Function not implemented";
      case EOVERFLOW:   return "EOVERFLOW: Value too large "
                               "for defined data type";
      case EBUSY:       return "EBUSY: Device or resource busy";
      case ETIMEDOUT:   return "ETIMEDOUT: Connection timed out";
      case EDEADLK:     return "EDEADLK: Resource deadlock would occur";
      case EOPNOTSUPP:  return "EOPNOTSUPP: Operation not supported on "
                               "transport endpoint"; /* honest, guv */
      default:          return "tc_intercepts.c: lame_strerror(): "
                               "unhandled case -- please fix me!";
   }
}


/*----------------------------------------------------------------*/
/*--- pthread_create, pthread_join, pthread_exit               ---*/
/*----------------------------------------------------------------*/

static void* ThreadSanitizerStartThread ( void* xargsV )
{
   volatile Word volatile* xargs = (volatile Word volatile*) xargsV;
   void*(*fn)(void*) = (void*(*)(void*))xargs[0];
   void* arg         = (void*)xargs[1];
   pthread_t me = pthread_self();
   /* Tell the tool what my pthread_t is. */
   DO_CREQ_v_W(TSREQ_SET_MY_PTHREAD_T, pthread_t,me);
   /* allow the parent to proceed.  We can't let it proceed until
      we're ready because (1) we need to make sure it doesn't exit and
      hence deallocate xargs[] while we still need it, and (2) we
      don't want either parent nor child to proceed until the tool has
      been notified of the child's pthread_t. */
   xargs[2] = 0;
   /* Now we can no longer safely use xargs[]. */
   return (void*) fn( (void*)arg );
}

// pthread_create
PTH_FUNC(int, pthreadZucreateZAZa, // pthread_create@*
              pthread_t *thread, const pthread_attr_t *attr,
              void *(*start) (void *), void *arg)
{
   int    ret;
   OrigFn fn;
   volatile Word xargs[3];

   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_create wrapper"); fflush(stderr);
   }
   xargs[0] = (Word)start;
   xargs[1] = (Word)arg;
   xargs[2] = 1; /* serves as a spinlock -- sigh */

   IGNORE_ALL_BEGIN();
     CALL_FN_W_WWWW(ret, fn, thread,attr,ThreadSanitizerStartThread,&xargs[0]);
   IGNORE_ALL_END();

   if (ret == 0) {
      /* we have to wait for the child to notify the tool of its
         pthread_t before continuing */
      while (xargs[2] != 0) {
         /* Do nothing.  We need to spin until the child writes to
            xargs[2].  However, that can lead to starvation in the
            child and very long delays (eg, tc19_shadowmem on
            ppc64-linux Fedora Core 6).  So yield the cpu if we can,
            to let the child run at the earliest available
            opportunity. */
         sched_yield();
      }
   } else { 
      DO_PthAPIerror( "pthread_create", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: pth_create -> %d >>\n", ret);
   }
   return ret;
}

// pthread_join
PTH_FUNC(int, pthreadZujoin, // pthread_join
              pthread_t thread, void** value_pointer)
{
   int ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_join wrapper"); fflush(stderr);
   }

   CALL_FN_W_WW(ret, fn, thread,value_pointer);

   /* At least with NPTL as the thread library, this is safe because
      it is guaranteed (by NPTL) that the joiner will completely gone
      before pthread_join (the original) returns.  See email below.*/
   if (ret == 0 /*success*/) {
      DO_CREQ_v_W(TSREQ_PTHREAD_JOIN_POST, pthread_t,thread);
   } else { 
      DO_PthAPIerror( "pthread_join", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: pth_join -> %d >>\n", ret);
   }
   return ret;
}

/* Behaviour of pthread_join on NPTL:

Me:
I have a question re the NPTL pthread_join implementation.

  Suppose I am the thread 'stayer'.  

  If I call pthread_join(quitter), is it guaranteed that the
  thread 'quitter' has really exited before pthread_join returns?

  IOW, is it guaranteed that 'quitter' will not execute any further
  instructions after pthread_join returns?

I believe this is true based on the following analysis of
glibc-2.5 sources.  However am not 100% sure and would appreciate
confirmation.

  'quitter' will be running start_thread() in nptl/pthread_create.c

  The last action of start_thread() is to exit via
  __exit_thread_inline(0), which simply does sys_exit 
  (nptl/pthread_create.c:403)

  'stayer' meanwhile is waiting for lll_wait_tid (pd->tid) 
  (call at nptl/pthread_join.c:89)

  As per comment at nptl/sysdeps/unix/sysv/linux/i386/lowlevellock.h:536,
  lll_wait_tid will not return until kernel notifies via futex
  wakeup that 'quitter' has terminated.

  Hence pthread_join cannot return until 'quitter' really has
  completely disappeared.

Drepper:
>   As per comment at nptl/sysdeps/unix/sysv/linux/i386/lowlevellock.h:536,
>   lll_wait_tid will not return until kernel notifies via futex
>   wakeup that 'quitter' has terminated.
That's the key.  The kernel resets the TID field after the thread is
done.  No way the joiner can return before the thread is gone.
*/


/*----------------------------------------------------------------*/
/*--- pthread_mutex_t functions                                ---*/
/*----------------------------------------------------------------*/

/* Handled:   pthread_mutex_init pthread_mutex_destroy
              pthread_mutex_lock
              pthread_mutex_trylock pthread_mutex_timedlock
              pthread_mutex_unlock

   Unhandled: pthread_spin_init pthread_spin_destroy 
              pthread_spin_lock
              pthread_spin_trylock
              pthread_spin_unlock
*/

// pthread_mutex_init
PTH_FUNC(int, pthreadZumutexZuinit, // pthread_mutex_init
              pthread_mutex_t *mutex,
              pthread_mutexattr_t* attr)
{
   int    ret;
   long   mbRec;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxinit %p", mutex); fflush(stderr);
   }

   mbRec = 0;
   if (attr) {
      int ty, zzz;
      zzz = pthread_mutexattr_gettype(attr, &ty);
      if (zzz == 0 && ty == PTHREAD_MUTEX_RECURSIVE)
         mbRec = 1;
   }

   CALL_FN_W_WW(ret, fn, mutex,attr);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_CREATE_POST,
                   pthread_mutex_t*,mutex, long,mbRec);
   } else { 
      DO_PthAPIerror( "pthread_mutex_init", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: mxinit -> %d >>\n", ret);
   }
   return ret;
}


// pthread_mutex_destroy
PTH_FUNC(int, pthreadZumutexZudestroy, // pthread_mutex_destroy
              pthread_mutex_t *mutex)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxdestroy %p", mutex); fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_DESTROY_PRE,
               pthread_mutex_t*,mutex);

   CALL_FN_W_W(ret, fn, mutex);

   if (ret != 0) {
      DO_PthAPIerror( "pthread_mutex_destroy", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: mxdestroy -> %d >>\n", ret);
   }
   return ret;
}


// pthread_mutex_lock
PTH_FUNC(int, pthreadZumutexZulock, // pthread_mutex_lock
              pthread_mutex_t *mutex)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxlock %p", mutex); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                 pthread_mutex_t*,mutex, long, 1/*is_w*/, long,0/*!isTryLock*/);

   CALL_FN_W_W(ret, fn, mutex);

   /* There's a hole here: libpthread now knows the lock is locked,
      but the tool doesn't, so some other thread could run and detect
      that the lock has been acquired by someone (this thread).  Does
      this matter?  Not sure, but I don't think so. */

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                  pthread_mutex_t*,mutex, long, 1);
   } else { 
      DO_PthAPIerror( "pthread_mutex_lock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: mxlock -> %d >>\n", ret);
   }
   return ret;
}


// pthread_mutex_trylock.  The handling needed here is very similar
// to that for pthread_mutex_lock, except that we need to tell
// the pre-lock creq that this is a trylock-style operation, and
// therefore not to complain if the lock is nonrecursive and 
// already locked by this thread -- because then it'll just fail
// immediately with EBUSY.
PTH_FUNC(int, pthreadZumutexZutrylock, // pthread_mutex_trylock
              pthread_mutex_t *mutex)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxtrylock %p", mutex); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                pthread_mutex_t*,mutex, long, 1/*is_w*/, long,1/*isTryLock*/);

   CALL_FN_W_W(ret, fn, mutex);

   /* There's a hole here: libpthread now knows the lock is locked,
      but the tool doesn't, so some other thread could run and detect
      that the lock has been acquired by someone (this thread).  Does
      this matter?  Not sure, but I don't think so. */

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                  pthread_mutex_t*,mutex, long, 1);
   } else { 
      if (ret != EBUSY)
         DO_PthAPIerror( "pthread_mutex_trylock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: mxtrylock -> %d >>\n", ret);
   }
   return ret;
}


// pthread_mutex_timedlock.  Identical logic to pthread_mutex_trylock.
PTH_FUNC(int, pthreadZumutexZutimedlock, // pthread_mutex_timedlock
	 pthread_mutex_t *mutex,
         void* timeout)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxtimedlock %p %p", mutex, timeout); 
      fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                pthread_mutex_t*,mutex, long, 1/*is_w*/, long,1/*isTryLock-ish*/);

   CALL_FN_W_WW(ret, fn, mutex,timeout);

   /* There's a hole here: libpthread now knows the lock is locked,
      but the tool doesn't, so some other thread could run and detect
      that the lock has been acquired by someone (this thread).  Does
      this matter?  Not sure, but I don't think so. */

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                  pthread_mutex_t*,mutex, long, 1);
   } else { 
      if (ret != ETIMEDOUT)
         DO_PthAPIerror( "pthread_mutex_timedlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: mxtimedlock -> %d >>\n", ret);
   }
   return ret;
}


// pthread_mutex_unlock
PTH_FUNC(int, pthreadZumutexZuunlock, // pthread_mutex_unlock
              pthread_mutex_t *mutex)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_mxunlk %p", mutex); fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_UNLOCK_PRE,
               pthread_mutex_t*,mutex);

   CALL_FN_W_W(ret, fn, mutex);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_UNLOCK_POST,
                  pthread_mutex_t*,mutex);
   } else { 
      DO_PthAPIerror( "pthread_mutex_unlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " mxunlk -> %d >>\n", ret);
   }
   return ret;
}


/*----------------------------------------------------------------*/
/*--- pthread_cond_t functions                                 ---*/
/*----------------------------------------------------------------*/

/* Handled:   pthread_cond_wait pthread_cond_timedwait
              pthread_cond_signal pthread_cond_broadcast

   Unhandled: pthread_cond_init pthread_cond_destroy
              -- are these important?
*/

// pthread_cond_wait
PTH_FUNC(int, pthreadZucondZuwaitZAZa, // pthread_cond_wait@*
              pthread_cond_t* cond, pthread_mutex_t* mutex)
{
  int ret;
  OrigFn fn;

  VALGRIND_GET_ORIG_FN(fn);

  if (TRACE_PTH_FNS) {
    fprintf(stderr, "<< pthread_cond_wait %p %p", cond, mutex);
    fflush(stderr);
  }

  DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_PRE,
               pthread_cond_t*,cond, pthread_mutex_t*,mutex);


  CALL_FN_W_WW(ret, fn, cond,mutex);


  DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_POST,
               pthread_cond_t*,cond, pthread_mutex_t*,mutex);

  if (ret != 0) {
    DO_PthAPIerror( "pthread_cond_wait", ret );
  }

  if (TRACE_PTH_FNS) {
    fprintf(stderr, " cowait -> %d >>\n", ret);
  }

  return ret;
}


// pthread_cond_timedwait
PTH_FUNC(int, pthreadZucondZutimedwaitZAZa, // pthread_cond_timedwait@*
         pthread_cond_t* cond, pthread_mutex_t* mutex, 
         struct timespec* abstime)
{
   int ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_cond_timedwait %p %p %p", 
                      cond, mutex, abstime);
      fflush(stderr);
   }

   /* Tell the tool a cond-wait is about to happen, so it can check
      for bogus argument values.  In return it tells us whether it
      thinks the mutex is valid or not. */
   DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_PRE,
                pthread_cond_t*,cond, pthread_mutex_t*,mutex);


   CALL_FN_W_WWW(ret, fn, cond,mutex,abstime);

   if (ret == 0) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_POST,
                   pthread_cond_t*,cond, pthread_mutex_t*,mutex);
   } else {
      DO_CREQ_v_WW(TSREQ_PTHREAD_COND_TWAIT_POST,
                   pthread_cond_t*,cond, pthread_mutex_t*,mutex);

   }

   if (ret != 0 && ret != ETIMEDOUT) {
      DO_PthAPIerror( "pthread_cond_timedwait", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " cotimedwait -> %d >>\n", ret);
   }

   return ret;
}


// pthread_cond_signal
PTH_FUNC(int, pthreadZucondZusignalZAZa, // pthread_cond_signal@*
              pthread_cond_t* cond)
{
   int ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_cond_signal %p", cond);
      fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_COND_SIGNAL_PRE,
               pthread_cond_t*,cond);

   CALL_FN_W_W(ret, fn, cond);

   if (ret != 0) {
      DO_PthAPIerror( "pthread_cond_signal", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " cosig -> %d >>\n", ret);
   }

   return ret;
}


// pthread_cond_broadcast
// Note, this is pretty much identical, from a dependency-graph
// point of view, with cond_signal, so the code is duplicated.
// Maybe it should be commoned up.
PTH_FUNC(int, pthreadZucondZubroadcastZAZa, // pthread_cond_broadcast@*
              pthread_cond_t* cond)
{
   int ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_broadcast_signal %p", cond);
      fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_COND_BROADCAST_PRE,
               pthread_cond_t*,cond);

   CALL_FN_W_W(ret, fn, cond);

   if (ret != 0) { 
      DO_PthAPIerror( "pthread_cond_broadcast", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " cobro -> %d >>\n", ret);
   }

   return ret;
}


static void do_wait_pre_and_post(void *cv, void *mu) {
  // fprintf(stderr, "do_wait_pre_and_post: %p %p\n", cv, mu);
  DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_PRE, void *, cv, void *, mu);
  DO_CREQ_v_WW(TSREQ_PTHREAD_COND_WAIT_POST, void *, 0, void *, 0);
}

/*----------------------------------------------------------------*/
/*--- pthread_barrier_t functions                              ---*/
/*----------------------------------------------------------------*/
PTH_FUNC(int, pthreadZubarrierZuwait, // pthread_barrier_wait. 
              pthread_barrier_t* b)
{
   int ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_barrier_wait %p", b);
      fflush(stderr);
   }

   // We blocked, signal. 
   DO_CREQ_v_W(TSREQ_PTHREAD_COND_BROADCAST_PRE,
               void*,b);
   CALL_FN_W_W(ret, fn, b);

   // FIXME: handle ret 

   // We unblocked, finish wait. 
   do_wait_pre_and_post(b, 0);

   if (TRACE_PTH_FNS) {
      fprintf(stderr, "  pthread_barrier_wait -> %d >>\n", ret);
   }

   return ret;
}




/*----------------------------------------------------------------*/
/*--- pthread_rwlock_t functions                               ---*/
/*----------------------------------------------------------------*/

/* Handled:   pthread_rwlock_init pthread_rwlock_destroy
              pthread_rwlock_rdlock 
              pthread_rwlock_wrlock
              pthread_rwlock_unlock

   Unhandled: pthread_rwlock_timedrdlock
              pthread_rwlock_tryrdlock

              pthread_rwlock_timedwrlock
              pthread_rwlock_trywrlock
*/

// pthread_rwlock_init
PTH_FUNC(int, pthreadZurwlockZuinit, // pthread_rwlock_init
              pthread_rwlock_t *rwl,
              pthread_rwlockattr_t* attr)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_init %p", rwl); fflush(stderr);
   }

   CALL_FN_W_WW(ret, fn, rwl,attr);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_CREATE_POST,
                  pthread_rwlock_t*,rwl);
   } else { 
      DO_PthAPIerror( "pthread_rwlock_init", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_init -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_destroy
PTH_FUNC(int, pthreadZurwlockZudestroy, // pthread_rwlock_destroy
              pthread_rwlock_t *rwl)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_destroy %p", rwl); fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_DESTROY_PRE,
               pthread_rwlock_t*,rwl);

   CALL_FN_W_W(ret, fn, rwl);

   if (ret != 0) {
      DO_PthAPIerror( "pthread_rwlock_destroy", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_destroy -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_wrlock
PTH_FUNC(int, pthreadZurwlockZuwrlock, // pthread_rwlock_wrlock
	 pthread_rwlock_t* rwlock)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_wlk %p", rwlock); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                 pthread_rwlock_t*,rwlock, 
                 long,1/*isW*/, long,0/*!isTryLock*/);

   CALL_FN_W_W(ret, fn, rwlock);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                   pthread_rwlock_t*,rwlock, long,1/*isW*/);
   } else { 
      DO_PthAPIerror( "pthread_rwlock_wrlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_wlk -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_rdlock
PTH_FUNC(int, pthreadZurwlockZurdlock, // pthread_rwlock_rdlock
	 pthread_rwlock_t* rwlock)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_rlk %p", rwlock); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                 pthread_rwlock_t*,rwlock,
                 long,0/*!isW*/, long,0/*!isTryLock*/);

   CALL_FN_W_W(ret, fn, rwlock);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                   pthread_rwlock_t*,rwlock, long,0/*!isW*/);
   } else { 
      DO_PthAPIerror( "pthread_rwlock_rdlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_rlk -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_trywrlock
PTH_FUNC(int, pthreadZurwlockZutrywrlock, // pthread_rwlock_trywrlock
	 pthread_rwlock_t* rwlock)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_trywlk %p", rwlock); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                 pthread_rwlock_t*,rwlock, 
                 long,1/*isW*/, long,1/*isTryLock*/);

   CALL_FN_W_W(ret, fn, rwlock);

   /* There's a hole here: libpthread now knows the lock is locked,
      but the tool doesn't, so some other thread could run and detect
      that the lock has been acquired by someone (this thread).  Does
      this matter?  Not sure, but I don't think so. */

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                   pthread_rwlock_t*,rwlock, long,1/*isW*/);
   } else { 
      if (ret != EBUSY)
         DO_PthAPIerror( "pthread_rwlock_trywrlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_trywlk -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_tryrdlock
PTH_FUNC(int, pthreadZurwlockZutryrdlock, // pthread_rwlock_tryrdlock
	 pthread_rwlock_t* rwlock)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_tryrlk %p", rwlock); fflush(stderr);
   }

   DO_CREQ_v_WWW(TSREQ_PTHREAD_RWLOCK_LOCK_PRE,
                 pthread_rwlock_t*,rwlock, 
                 long,0/*!isW*/, long,1/*isTryLock*/);

   CALL_FN_W_W(ret, fn, rwlock);

   /* There's a hole here: libpthread now knows the lock is locked,
      but the tool doesn't, so some other thread could run and detect
      that the lock has been acquired by someone (this thread).  Does
      this matter?  Not sure, but I don't think so. */

   if (ret == 0 /*success*/) {
      DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,
                   pthread_rwlock_t*,rwlock, long,0/*!isW*/);
   } else { 
      if (ret != EBUSY)
         DO_PthAPIerror( "pthread_rwlock_tryrdlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_tryrlk -> %d >>\n", ret);
   }
   return ret;
}


// pthread_rwlock_unlock
PTH_FUNC(int, pthreadZurwlockZuunlock, // pthread_rwlock_unlock
	 pthread_rwlock_t* rwlock)
{
   int    ret;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   if (TRACE_PTH_FNS) {
      fprintf(stderr, "<< pthread_rwl_unlk %p", rwlock); fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_UNLOCK_PRE,
               pthread_rwlock_t*,rwlock);

   CALL_FN_W_W(ret, fn, rwlock);

   if (ret == 0 /*success*/) {
      DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_UNLOCK_POST,
                  pthread_rwlock_t*,rwlock);
   } else { 
      DO_PthAPIerror( "pthread_rwlock_unlock", ret );
   }

   if (TRACE_PTH_FNS) {
      fprintf(stderr, " :: rwl_unlk -> %d >>\n", ret);
   }
   return ret;
}


/*----------------------------------------------------------------*/
/*--- POSIX semaphores                                         ---*/
/*----------------------------------------------------------------*/

#include <semaphore.h>

#define TRACE_SEM_FNS 0

/* Handled: 
     int sem_init(sem_t *sem, int pshared, unsigned value);
     int sem_destroy(sem_t *sem);
     int sem_wait(sem_t *sem);
     int sem_post(sem_t *sem);

   Unhandled:
     int sem_trywait(sem_t *sem);
     int sem_timedwait(sem_t *restrict sem,
                       const struct timespec *restrict abs_timeout);
*/

/* glibc-2.5 has sem_init@@GLIBC_2.2.5 (amd64-linux)
             and sem_init@@GLIBC_2.1 (x86-linux): match sem_init@* */
PTH_FUNC(int, semZuinitZAZa, sem_t* sem, int pshared, unsigned long value)
{
   OrigFn fn;
   int    ret;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_SEM_FNS) {
      fprintf(stderr, "<< sem_init(%p,%d,%lu) ", sem,pshared,value);
      fflush(stderr);
   }

   CALL_FN_W_WWW(ret, fn, sem,pshared,value);

   if (ret == 0) {
      DO_CREQ_v_WW(TSREQ_POSIX_SEM_INIT_POST,
                   sem_t*, sem, unsigned long, value);
   } else {
      DO_PthAPIerror( "sem_init", errno );
   }

   if (TRACE_SEM_FNS) {
      fprintf(stderr, " sem_init -> %d >>\n", ret);
      fflush(stderr);
   }

   return ret;
}


/* glibc-2.5 has sem_destroy@@GLIBC_2.2.5 (amd64-linux)
             and sem_destroy@@GLIBC_2.1 (x86-linux); match sem_destroy@* */
PTH_FUNC(int, semZudestroyZAZa, sem_t* sem)
{
   OrigFn fn;
   int    ret;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_SEM_FNS) {
      fprintf(stderr, "<< sem_destroy(%p) ", sem);
      fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_POSIX_SEM_DESTROY_PRE, sem_t*, sem);

   CALL_FN_W_W(ret, fn, sem);

   if (ret != 0) {
      DO_PthAPIerror( "sem_destroy", errno );
   }

   if (TRACE_SEM_FNS) {
      fprintf(stderr, " sem_destroy -> %d >>\n", ret);
      fflush(stderr);
   }

   return ret;
}


/* glibc-2.5 has sem_wait (amd64-linux); match sem_wait
             and sem_wait@@GLIBC_2.1 (x86-linux); match sem_wait@* */
/* wait: decrement semaphore - acquire lockage */
static int sem_wait_WRK(sem_t* sem, const char *name, int is_try)
{
   OrigFn fn;
   int    ret;
   VALGRIND_GET_ORIG_FN(fn);

   if (TRACE_SEM_FNS) {
      fprintf(stderr, "<< %s(%p) ", name, sem);
      fflush(stderr);
   }

   CALL_FN_W_W(ret, fn, sem);

   if (ret == 0) {
      DO_CREQ_v_W(TSREQ_POSIX_SEM_WAIT_POST, sem_t*,sem);
   } else {
      if (!is_try) {
         DO_PthAPIerror( name, errno );
      }
   }

   if (TRACE_SEM_FNS) {
      fprintf(stderr, " %s -> %d >>\n", name, ret);
      fflush(stderr);
   }

   return ret;
}
PTH_FUNC(int, semZuwait, sem_t* sem) { /* sem_wait */
   return sem_wait_WRK(sem, "sem_wait", 0);
}
PTH_FUNC(int, semZuwaitZAZa, sem_t* sem) { /* sem_wait@* */
   return sem_wait_WRK(sem, "sem_wait", 0);
}
PTH_FUNC(int, semZutrywait, sem_t* sem) { /* sem_trywait */
   return sem_wait_WRK(sem, "sem_trywait", 1);
}
PTH_FUNC(int, semZutrywaitZAZa, sem_t* sem) { /* sem_trywait@* */
   return sem_wait_WRK(sem, "sem_trywait", 1);
}




/* glibc-2.5 has sem_post (amd64-linux); match sem_post
             and sem_post@@GLIBC_2.1 (x86-linux); match sem_post@* */
/* post: increment semaphore - release lockage */
static int sem_post_WRK(OrigFn fn, sem_t* sem)
{
   int    ret;


   if (TRACE_SEM_FNS) {
      fprintf(stderr, "<< sem_post(%p) ", sem);
      fflush(stderr);
   }

   DO_CREQ_v_W(TSREQ_POSIX_SEM_POST_PRE, sem_t*,sem);

   CALL_FN_W_W(ret, fn, sem);

   if (ret != 0) {
      DO_PthAPIerror( "sem_post", errno );
   }

   if (TRACE_SEM_FNS) {
      fprintf(stderr, " sem_post -> %d >>\n", ret);
      fflush(stderr);
   }

   return ret;
}
PTH_FUNC(int, semZupost, sem_t* sem) { /* sem_post */
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   return sem_post_WRK(fn, sem);
}
PTH_FUNC(int, semZupostZAZa, sem_t* sem) { /* sem_post@* */
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   return sem_post_WRK(fn, sem);
}


/* From man page: 
   sem_t *sem_open(const char *name, int oflag, ...);
   ...
   The oflag argument controls whether the semaphore is created or merely 
   accessed by the call to sem_open(). The following flag bits may be 
   set in oflag:
   ...
   If O_CREAT is set and the semaphore already exists, then O_CREAT has no 
   effect, except as noted under O_EXCL. Otherwise, sem_open() creates a 
   named semaphore. The O_CREAT flag requires a third and a fourth 
   argument: mode, which is of type mode_t, and value, which is of 
   type unsigned int. The semaphore is created with an initial value of value.
*/
static sem_t *sem_open_WRK(OrigFn fn, 
                           const char *name, int oflag, 
                           mode_t mode, unsigned int value) {

   sem_t *ret;
   CALL_FN_W_WWWW(ret, fn, name, oflag, mode, value);
   if ((oflag & O_CREAT) && 
       value > 0 && 
       ret != SEM_FAILED) {
     // This semaphore has been created with a non-zero value. 
     // The semaphore is initialized only on the first call to sem_open, 
     // next call will return an existing semaphore.
     // Ideally, we need to handle it like sem_init with a non-zero value.
     // But in such case we also need to handle sem_unlink. 
     //
     // To avoid this complexity we simply do a POST here.
     DO_CREQ_v_W(TSREQ_POSIX_SEM_POST_PRE, sem_t*, ret);
   }
   return ret;
}

PTH_FUNC(sem_t *, semZuopen, const char *name, int oflag, 
         mode_t mode, unsigned int value) { /* sem_open */
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   return sem_open_WRK(fn, name, oflag, mode, value);
}

PTH_FUNC(sem_t *, semZuopenZAZa, const char *name, int oflag, 
         mode_t mode, unsigned int value) { /* sem_open@* */
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   return sem_open_WRK(fn, name, oflag, mode, value);
}



// socket/file IO that creates happens-before arcs.

static void *SocketMagic(int s) {
  return (void*)0xDEADFBAD;
}





NONE_FUNC(int, epoll_wait, int epfd, void * events, int maxevents, int timeout) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
//   fprintf(stderr, "T%d socket epoll_wait: %d\n", VALGRIND_TS_THREAD_ID, epfd);
   o = SocketMagic(epfd);
   do_wait_pre_and_post(o, 0);
   CALL_FN_W_WWWW(ret, fn, epfd, events, maxevents, timeout);
   return ret;
}

NONE_FUNC(int, epoll_ctl, int epfd, int op, int fd, void *event) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
//   fprintf(stderr, "T%d socket epoll_ctl: %d\n", VALGRIND_TS_THREAD_ID, epfd);
   o = SocketMagic(epfd);
   DO_CREQ_v_W(TSREQ_PTHREAD_COND_SIGNAL_PRE, void*, o);
   CALL_FN_W_WWWW(ret, fn, epfd, op, fd, event);
   return ret;
}



PTH_FUNC(long, send, int s, void *buf, long len, int flags) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
//   fprintf(stderr, "T%d socket send: %d %ld\n", VALGRIND_TS_THREAD_ID, s, len);
   o = SocketMagic(s);
   DO_CREQ_v_W(TSREQ_PTHREAD_COND_SIGNAL_PRE, void*, o);
   CALL_FN_W_WWWW(ret, fn, s, buf, len, flags);
   return ret;
}

PTH_FUNC(long, recv, int s, void *buf, long len, int flags) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
   CALL_FN_W_WWWW(ret, fn, s, buf, len, flags);
//   fprintf(stderr, "T%d socket recv: %d %ld %ld\n", VALGRIND_TS_THREAD_ID, s, len, ret);
   o = SocketMagic(s);
   do_wait_pre_and_post(o, 0);
   return ret;
}


PTH_FUNC(long, read, int s, void *a2, long count) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
   CALL_FN_W_WWW(ret, fn, s, a2, count);
//   fprintf(stderr, "T%d socket read: %d %ld %ld\n", VALGRIND_TS_THREAD_ID, s, count, ret);
   o = SocketMagic(s);
   do_wait_pre_and_post(o, 0);
   return ret;
}

PTH_FUNC(long, write, int s, void *a2, long a3) {
   OrigFn fn;
   long    ret;
   void *o;
   VALGRIND_GET_ORIG_FN(fn);
//   fprintf(stderr, "T%d socket write: %d\n", VALGRIND_TS_THREAD_ID, s);
   o = SocketMagic(s);
   DO_CREQ_v_W(TSREQ_PTHREAD_COND_SIGNAL_PRE, void*, o);
   CALL_FN_W_WWW(ret, fn, s, a2, a3);
   return ret;
}

/* 
  Support for pthread_once and function-level static objects.

  pthread_once is supported by simply ignoring everything that happens 
  inside pthread_once.

  Another approach would be to SIGNAL when pthread_once with a given 
  pthread_once_t is called for the first time and to WAIT after 
  each pthread_once. But implementing this is a bit tricky and probably
  not worth it. 

  Thread safe initialization of function-level static objects is 
  supported in gcc (strarting from 4.something). 
  The generated code calls __cxa_guard_acquire (which in turn 
  calls pthread_once) and then calls __cxa_guard_release. 
  We simply ignore everything between __cxa_guard_acquire 
  and __cxa_guard_release. pthread_once interceptor does the rest.

  For examples, see test106 and test108 at 
  http://code.google.com/p/data-race-test/source/browse/trunk/unittest/racecheck_unittest.cc
*/

PTH_FUNC(int, pthreadZuonce, void *ctl, void *rtn) {
   OrigFn fn;
   int    ret;
   VALGRIND_GET_ORIG_FN(fn);
   IGNORE_ALL_BEGIN();
   // fprintf(stderr, "T%d: ->pthread_once\n", VALGRIND_TS_THREAD_ID);
   CALL_FN_W_WW(ret, fn, ctl, rtn);
   // fprintf(stderr, "T%d: <-pthread_once\n", VALGRIND_TS_THREAD_ID);
   IGNORE_ALL_END();
   return ret;
}

LIBSTDCXX_FUNC(long, ZuZucxaZuguardZuacquire, void *p) {
   OrigFn fn;
   long    ret;
   VALGRIND_GET_ORIG_FN(fn);
   // fprintf(stderr, "T%d: ->__cxa_guard_acquire\n", VALGRIND_TS_THREAD_ID());
   CALL_FN_W_W(ret, fn, p);
   IGNORE_ALL_BEGIN();
   // fprintf(stderr, "T%d: <-__cxa_guard_acquire\n", VALGRIND_TS_THREAD_ID());
   return ret;
}
LIBSTDCXX_FUNC(long, ZuZucxaZuguardZurelease, void *p) {
   OrigFn fn;
   long    ret;
   VALGRIND_GET_ORIG_FN(fn);
   // fprintf(stderr, "T%d: ->__cxa_guard_release\n", VALGRIND_TS_THREAD_ID());
   CALL_FN_W_W(ret, fn, p);
   // fprintf(stderr, "T%d: <-__cxa_guard_release\n", VALGRIND_TS_THREAD_ID());
   IGNORE_ALL_END();
   return ret;
}




/*----------------------------------------------------------------*/
/*--- Replace glibc's wretched optimised string fns (again!)   ---*/
/*----------------------------------------------------------------*/


/* Why we have to do all this nonsense:

   Some implementations of strlen may read up to 7 bytes past the end
   of the string thus touching memory which may not belong to this
   string.

   Such race is benign because the data read past the end of the
   string is not used.
*/



// --- MEMCPY -----------------------------------------------------
//
#define MEMCPY(soname, fnname) \
   void* VG_REPLACE_FUNCTION_ZU(soname,fnname) \
            ( void *dst, const void *src, SizeT len ); \
   void* VG_REPLACE_FUNCTION_ZU(soname,fnname) \
            ( void *dst, const void *src, SizeT len ) \
   { \
      register char *d; \
      register char *s; \
      \
      if (len == 0) \
         return dst; \
      \
      if ( dst > src ) { \
         d = (char *)dst + len - 1; \
         s = (char *)src + len - 1; \
         while ( len >= 4 ) { \
            *d-- = *s--; \
            *d-- = *s--; \
            *d-- = *s--; \
            *d-- = *s--; \
            len -= 4; \
         } \
         while ( len-- ) { \
            *d-- = *s--; \
         } \
      } else if ( dst < src ) { \
         d = (char *)dst; \
         s = (char *)src; \
         while ( len >= 4 ) { \
            *d++ = *s++; \
            *d++ = *s++; \
            *d++ = *s++; \
            *d++ = *s++; \
            len -= 4; \
         } \
         while ( len-- ) { \
            *d++ = *s++; \
         } \
      } \
      return dst; \
   }

MEMCPY(m_libc_soname, memcpy)
MEMCPY(m_ld_so_1,     memcpy) /* ld.so.1 */
MEMCPY(m_ld64_so_1,   memcpy) /* ld64.so.1 */
/* icc9 blats these around all over the place.  Not only in the main
   executable but various .so's.  They are highly tuned and read
   memory beyond the source boundary (although work correctly and
   never go across page boundaries), so give errors when run natively,
   at least for misaligned source arg.  Just intercepting in the exe
   only until we understand more about the problem.  See
   http://bugs.kde.org/show_bug.cgi?id=139776
 */
MEMCPY(NONE, _intel_fast_memcpy)


// --- STRCHR and INDEX -------------------------------------------
//
#define STRCHR(soname, fnname) \
   char* VG_REPLACE_FUNCTION_ZU(soname,fnname) ( const char* s, int c ); \
   char* VG_REPLACE_FUNCTION_ZU(soname,fnname) ( const char* s, int c ) \
   { \
      UChar  ch = (UChar)((UInt)c); \
      UChar* p  = (UChar*)s; \
      while (True) { \
         if (*p == ch) return p; \
         if (*p == 0) return NULL; \
         p++; \
      } \
   }

// Apparently index() is the same thing as strchr()
STRCHR(m_libc_soname,          strchr)
STRCHR(m_ld_linux_so_2,        strchr)
STRCHR(m_ld_linux_x86_64_so_2, strchr)
STRCHR(m_libc_soname,          index)
STRCHR(m_ld_linux_so_2,        index)
STRCHR(m_ld_linux_x86_64_so_2, index)


// --- STRCMP -----------------------------------------------------
//
#define STRCMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_ZU(soname,fnname) \
          ( const char* s1, const char* s2 ); \
   int VG_REPLACE_FUNCTION_ZU(soname,fnname) \
          ( const char* s1, const char* s2 ) \
   { \
      register unsigned char c1; \
      register unsigned char c2; \
      while (True) { \
         c1 = *(unsigned char *)s1; \
         c2 = *(unsigned char *)s2; \
         if (c1 != c2) break; \
         if (c1 == 0) break; \
         s1++; s2++; \
      } \
      if ((unsigned char)c1 < (unsigned char)c2) return -1; \
      if ((unsigned char)c1 > (unsigned char)c2) return 1; \
      return 0; \
   }

STRCMP(m_libc_soname,          strcmp)
STRCMP(m_ld_linux_x86_64_so_2, strcmp)
STRCMP(m_ld64_so_1,            strcmp)


// --- STRLEN -----------------------------------------------------
//
// Note that this replacement often doesn't get used because gcc inlines
// calls to strlen() with its own built-in version.  This can be very
// confusing if you aren't expecting it.  Other small functions in this file
// may also be inline by gcc.
#define STRLEN(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_ZU(soname,fnname)( const char* str ); \
   SizeT VG_REPLACE_FUNCTION_ZU(soname,fnname)( const char* str ) \
   { \
      SizeT i = 0; \
      while (str[i] != 0) i++; \
      return i; \
   }

STRLEN(m_libc_soname,          strlen)
STRLEN(m_ld_linux_so_2,        strlen)
STRLEN(m_ld_linux_x86_64_so_2, strlen)


// --- STRCPY -----------------------------------------------------
//
#define STRCPY(soname, fnname) \
   char* VG_REPLACE_FUNCTION_ZU(soname, fnname) ( char* dst, const char* src ); \
   char* VG_REPLACE_FUNCTION_ZU(soname, fnname) ( char* dst, const char* src ) \
   { \
      Char* dst_orig = dst; \
      \
      while (*src) *dst++ = *src++; \
      *dst = 0; \
      \
      return dst_orig; \
   }

STRCPY(m_libc_soname, strcpy)


//------------------------ Annotations ---------------- {{{1



#define ANN_FUNC(ret_ty, f, args...) \
    ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args); \
    ret_ty I_WRAP_SONAME_FNNAME_ZZ(NONE,f)(args)


#define ANN_TRACE(args...) \
    do{\
      if(TRACE_ANN_FNS){\
        int tid = VALGRIND_TS_THREAD_ID();\
        int sid = VALGRIND_TS_SEGMENT_ID();\
        fprintf(stderr, args);\
        if(tid != 999999 && sid != 999999) fflush(stderr);\
      }\
    }while(0)

ANN_FUNC(int, RunningOnValgrind, void) {
  return 1;
}

ANN_FUNC(void, AnnotateRWLockCreate, const char *file, int line, void *lock)
{
  const char *name = "AnnotateRWLockCreate";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, lock, file, line);
  DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_CREATE_POST, void*, lock, long, 0 /*non recur*/);
}

ANN_FUNC(void, AnnotateRWLockDestroy, const char *file, int line, void *lock)
{
  const char *name = "AnnotateRWLockDestroy";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, lock, file, line);
  DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_DESTROY_PRE, void*, lock);
}

ANN_FUNC(void, AnnotateRWLockAcquired, const char *file, int line, void *lock, int is_w)
{
  const char *name = "AnnotateRWLockAcquired";
  ANN_TRACE("--#%d %s[%p] rw=%d %s:%d\n", tid, name, lock, is_w, file, line);
  DO_CREQ_v_WW(TSREQ_PTHREAD_RWLOCK_LOCK_POST,  void*,lock,long, (long)is_w);
}

ANN_FUNC(void, AnnotateRWLockReleased, const char *file, int line, void *lock, int is_w)
{
  const char *name = "AnnotateRWLockReleased";
  ANN_TRACE("--#%d %s[%p] rw=%d %s:%d\n", tid, name, lock, is_w, file, line);
  DO_CREQ_v_W(TSREQ_PTHREAD_RWLOCK_UNLOCK_PRE, void*, lock);
}

ANN_FUNC(void, AnnotateCondVarWait, const char *file, int line, void *cv, void *lock)
{
  const char *name = "AnnotateCondVarWait";
  ANN_TRACE("--#%d %s[%p|%p] %s:%d\n", tid, name, cv, lock, file, line);
  do_wait_pre_and_post(cv, 0 /*lock is unused*/);
}

ANN_FUNC(void, AnnotateCondVarSignal, const char *file, int line, void *cv)
{
  const char *name = "AnnotateCondVarSignal";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, cv, file, line);
  DO_CREQ_v_W(TSREQ_PTHREAD_COND_SIGNAL_PRE, void*,cv);
}

ANN_FUNC(void, AnnotateCondVarSignalAll, const char *file, int line, void *cv)
{
  const char *name = "AnnotateCondVarSignalAll";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, cv, file, line);
  DO_CREQ_v_W(TSREQ_PTHREAD_COND_BROADCAST_PRE, void*,cv);
}


ANN_FUNC(void, AnnotatePCQCreate, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQCreate";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(TSREQ_PCQ_CREATE,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQDestroy, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQDestroy";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(TSREQ_PCQ_DESTROY,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQPut, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQPut";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(TSREQ_PCQ_PUT,   void*,pcq);
}

ANN_FUNC(void, AnnotatePCQGet, const char *file, int line, void *pcq)
{
  const char *name = "AnnotatePCQGet";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, pcq, file, line);
  DO_CREQ_v_W(TSREQ_PCQ_GET,   void*,pcq);
}

ANN_FUNC(void, AnnotateExpectRace, const char *file, int line, void *mem, char *description)
{
  const char *name = "AnnotateExpectRace";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_WWWW(TSREQ_EXPECT_RACE, void*,mem, char*,description, char*, file, long, (long)line);
}

ANN_FUNC(void, AnnotateBenignRace, const char *file, int line, void *mem, char *description)
{
  const char *name = "AnnotateBenignRace";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_WWWW(TSREQ_BENIGN_RACE, void*,mem, char*,description, char*, file, long, (long)line);
}

ANN_FUNC(void, AnnotateNewMemory, char *file, int line, void *mem, long size)
{
  const char *name = "AnnotateNewMemory";
  ANN_TRACE("--#%d %s[%p,%d] %s:%d\n", tid, name, mem, (int)size, file, line);
  DO_CREQ_v_WWWW(TSREQ_CLEAN_MEMORY, void*,mem, long, size, char*, file, long, (long)line);
}

ANN_FUNC(void, AnnotatePublishMemoryRange, char *file, int line, void *mem, long size)
{
  const char *name = "AnnotatePublishMemoryRange";
  ANN_TRACE("--#%d %s[%p,%d] %s:%d\n", tid, name, mem, (int)size, file, line);
  DO_CREQ_v_WW(TSREQ_PUBLISH_MEMORY_RANGE,   void*, mem, long, size);
}

ANN_FUNC(void, AnnotateIgnoreReadsBegin, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreReadsBegin";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(TSREQ_IGNORE_READS_BEGIN,   void*, mu);
}

ANN_FUNC(void, AnnotateIgnoreReadsEnd, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreReadsEnd";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(TSREQ_IGNORE_READS_END,   void*, mu);
}

ANN_FUNC(void, AnnotateIgnoreWritesBegin, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreWritesBegin";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(TSREQ_IGNORE_WRITES_BEGIN,   void*, mu);
}

ANN_FUNC(void, AnnotateIgnoreWritesEnd, char *file, int line, void *mu)
{
  const char *name = "AnnotateIgnoreWritesEnd";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(TSREQ_IGNORE_WRITES_END,   void*, mu);
}

ANN_FUNC(void, AnnotateThreadName, char *file, int line, const char *thread_name)
{
  const char *name = "AnnotateThreadName";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, thread_name, file, line);
  DO_CREQ_v_W(TSREQ_SET_THREAD_NAME, const char *, thread_name);
}



ANN_FUNC(void, AnnotateMutexIsUsedAsCondVar, char *file, int line, void *mu)
{
  const char *name = "AnnotateMutexIsUsedAsCondVar";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mu, file, line);
  DO_CREQ_v_W(TSREQ_MUTEX_IS_USED_AS_CONDVAR,   void*, mu);
}



ANN_FUNC(void, AnnotateTraceMemory, char *file, int line, void *mem)
{
  const char *name = "AnnotateTraceMemory";
  ANN_TRACE("--#%d %s[%p] %s:%d\n", tid, name, mem, file, line);
  DO_CREQ_v_W(TSREQ_TRACE_MEM,   void*, mem);
}

#undef TRACE_ANN_FNS 
#define TRACE_ANN_FNS 1

ANN_FUNC(void, AnnotateNoOp, char *file, int line, void *mem)
{
  const char *name = "AnnotateNoOp";
  IGNORE_ALL_BEGIN();
  ANN_TRACE("--#%d/%d %s[%p] %s:%d\n", tid, sid, name, mem, file, line);
  IGNORE_ALL_END();
}

ANN_FUNC(void, AnnotateSetVerbosity, char *file, int line, void *mem)
{
  const char *name = "AnnotateSetVerbosity";
  OrigFn fn;
  VALGRIND_GET_ORIG_FN(fn);
  fprintf(stderr, "%s fn=%p\n", name, (void*)fn.nraddr); 
  ANN_TRACE("--#%d/%d %s[%p] %s:%d\n", tid, sid, name, mem, file, line);
}



//-------------- Functions to Ignore -------------- {{{1
// For some functions we want to ignore everything that happens 
// after they were called and before they returned. 
// Is there any way that allows to do this via a command line?
#define WRAP_AND_IGNORE(soname, fnname) \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *a1, void *a2, void *a3, void *a4); \
  void* I_WRAP_SONAME_FNNAME_ZU(soname,fnname) (void *a1, void *a2, void *a3, void *a4) { \
    void* ret; \
    OrigFn fn;\
    VALGRIND_GET_ORIG_FN(fn);\
    IGNORE_ALL_BEGIN(); \
      CALL_FN_W_WWWW(ret, fn, a1, a2, a3, a4); \
    IGNORE_ALL_END(); \
    return ret; \
  }

// Ideally, this list should be passed via command line flags.
// But alas, valgrind can't do that...
// So, we have to list the functions during the tool build time.
// We do it in a separate file to simplify changes there.
#include "ts_wrap_and_ignore.h"

// {{{1 end
// vim:shiftwidth=2:softtabstop=2:expandtab
