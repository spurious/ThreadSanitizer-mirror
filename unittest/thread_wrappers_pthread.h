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
// Here we define few simple classes that wrap pthread primitives. 
//
// We need this to create unit tests for helgrind (or similar tool) 
// that will work with different threading frameworks. 
//
// If one needs to test helgrind's support for another threading library, 
// he/she can create a copy of this file and replace pthread_ calls 
// with appropriate calls to his/her library. 
//
// Note, that some of the methods defined here are annotated with 
// ANNOTATE_* macros defined in dynamic_annotations.h. 
//
// DISCLAIMER: the classes defined in this header file 
// are NOT intended for general use -- only for unit tests. 
//

#ifndef THREAD_WRAPPERS_PTHREAD_H
#define THREAD_WRAPPERS_PTHREAD_H

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <queue>
#include <stdio.h>

#include <sys/time.h>
#include <time.h>

#define DYNAMIC_ANNOTATIONS  // Enable dynamic annotations. 
#include "dynamic_annotations.h"

#include <assert.h>
#ifdef NDEBUG
# error "Pleeease, do not define NDEBUG"
#endif 
#define CHECK assert


/// Current time in milliseconds. 
static inline int64_t GetCurrentTimeMillis() {
  struct timeval now;
  gettimeofday(&now, NULL);
  return now.tv_sec * 1000 + now.tv_usec / 1000;
}

/// Copy tv to ts adding offset in milliseconds. 
static inline void timeval2timespec(timeval *const tv, 
                                     timespec *ts, 
                                     int64_t offset_milli) {
  const int64_t ten_9 = 1000000000LL;
  const int64_t ten_6 = 1000000LL;
  const int64_t ten_3 = 1000LL;
  int64_t now_nsec = (int64_t)tv->tv_sec * ten_9;
  now_nsec += (int64_t)tv->tv_usec * ten_3;
  int64_t then_nsec = now_nsec + offset_milli * ten_6;
  ts->tv_sec  = then_nsec / ten_9;
  ts->tv_nsec = then_nsec % ten_9;
}


class CondVar;

#ifndef NO_SPINLOCK
/// helgrind does not (yet) support spin locks, so we annotate them.
class SpinLock {
 public:
  SpinLock() {
    CHECK(0 == pthread_spin_init(&mu_, 0));
    ANNOTATE_RWLOCK_CREATE((void*)&mu_);
  }
  ~SpinLock() {
    ANNOTATE_RWLOCK_DESTROY((void*)&mu_);
    CHECK(0 == pthread_spin_destroy(&mu_));
  }
  void Lock() {
    CHECK(0 == pthread_spin_lock(&mu_));
    ANNOTATE_RWLOCK_ACQUIRED((void*)&mu_, 1);
  }
  void Unlock() {
    ANNOTATE_RWLOCK_RELEASED((void*)&mu_, 1);
    CHECK(0 == pthread_spin_unlock(&mu_));
  }
 private:
  pthread_spinlock_t mu_;
};
#endif // NO_SPINLOCK

/// Just a boolean condition. Used by Mutex::LockWhen and similar. 
class Condition {
 public:
  typedef bool (*func_t)(void*);

  template <typename T>
  Condition(bool (*func)(T*), T* arg) 
  : func_(reinterpret_cast<func_t>(func)), arg_(arg) {}

  bool Eval() { return func_(arg_); }
 private:
  func_t func_;
  void *arg_;

};


/// Wrapper for pthread_mutex_t.
///
/// pthread_mutex_t is *not* a reader-writer lock, 
/// so the methods like ReaderLock() aren't really reader locks. 
/// We can not use pthread_rwlock_t because it 
/// does not work with pthread_cond_t.
/// 
/// TODO: We still need to test reader locks with this class. 
/// Implement a mode where pthread_rwlock_t will be used 
/// instead of pthread_mutex_t (only when not used with CondVar or LockWhen). 
/// 
class Mutex {
  friend class CondVar;
 public: 
  Mutex() {
    CHECK(0 == pthread_mutex_init(&mu_, NULL));
    CHECK(0 == pthread_cond_init(&cv_, NULL));
  }
  ~Mutex() {
    CHECK(0 == pthread_cond_destroy(&cv_));
    CHECK(0 == pthread_mutex_destroy(&mu_));
  }
  void Lock()          { CHECK(0 == pthread_mutex_lock(&mu_));}
  bool TryLock()       { return (0 == pthread_mutex_trylock(&mu_));}
  void Unlock() {
    CHECK(0 == pthread_cond_signal(&cv_)); 
    CHECK(0 == pthread_mutex_unlock(&mu_));
  }
  void ReaderLock()    { Lock(); }
  bool ReaderTryLock() { return TryLock();}
  void ReaderUnlock()  { Unlock(); }

  void LockWhen(Condition cond)            { Lock(); WaitLoop(cond); }
  void ReaderLockWhen(Condition cond)      { Lock(); WaitLoop(cond); }
  void Await(Condition cond)               { WaitLoop(cond); }

  bool ReaderLockWhenWithTimeout(Condition cond, int millis)      
    { Lock(); return WaitLoopWithTimeout(cond, millis); }
  bool LockWhenWithTimeout(Condition cond, int millis)      
    { Lock(); return WaitLoopWithTimeout(cond, millis); }
  bool AwaitWithTimeout(Condition cond, int millis)      
    { return WaitLoopWithTimeout(cond, millis); }

 private:

  void WaitLoop(Condition cond) {
    while(cond.Eval() == false) {
      pthread_cond_wait(&cv_, &mu_);
    }
    ANNOTATE_CONDVAR_WAIT(&cv_, &mu_);
  }

  bool WaitLoopWithTimeout(Condition cond, int millis) {
    struct timeval now;
    struct timespec timeout;
    int retcode = 0;
    gettimeofday(&now, NULL);
    timeval2timespec(&now, &timeout, millis);

    while (cond.Eval() == false && retcode == 0) {
      retcode = pthread_cond_timedwait(&cv_, &mu_, &timeout);
    }
    if(retcode == 0) {
      ANNOTATE_CONDVAR_WAIT(&cv_, &mu_);
    }
    return cond.Eval();
  }

  // A hack. cv_ should be the first data member so that 
  // ANNOTATE_CONDVAR_WAIT(&MU, &MU) and ANNOTATE_CONDVAR_SIGNAL(&MU) works. 
  // (See also racecheck_unittest.cc)
  pthread_cond_t  cv_; 
  pthread_mutex_t mu_;
};

/// Wrapper for pthread_cond_t. 
class CondVar {
 public:
  CondVar()   { CHECK(0 == pthread_cond_init(&cv_, NULL)); }
  ~CondVar()  { CHECK(0 == pthread_cond_destroy(&cv_)); }
  void Wait(Mutex *mu) { CHECK(0 == pthread_cond_wait(&cv_, &mu->mu_)); }
  bool WaitWithTimeout(Mutex *mu, int millis) { 
    struct timeval now;
    struct timespec timeout;
    gettimeofday(&now, NULL);
    timeval2timespec(&now, &timeout, millis);
    return 0 != pthread_cond_timedwait(&cv_, &mu->mu_, &timeout);
  }
  void Signal() { CHECK(0 == pthread_cond_signal(&cv_)); }
  void SignalAll() { CHECK(0 == pthread_cond_broadcast(&cv_)); }
 private:
  pthread_cond_t cv_;
};


/// Wrapper for pthread_create()/pthread_join().
class MyThread {
 public: 
  typedef void *(*worker_t)(void*);

  MyThread(worker_t worker, void *arg = NULL) 
      :w_(worker), arg_(arg){}
  MyThread(void (*worker)(void), void *arg = NULL)   
      :w_(reinterpret_cast<worker_t>(worker)), arg_(arg){}
  MyThread(void (*worker)(void *), void *arg = NULL) 
      :w_(reinterpret_cast<worker_t>(worker)), arg_(arg){}

  ~MyThread(){ w_ = NULL; arg_ = NULL;}
  void Start(void *arg = NULL) { CHECK(0 == pthread_create(&t_, NULL, w_, arg));}
  void Join()                  { CHECK(0 == pthread_join(t_, NULL));}
 private:
  pthread_t t_;
  worker_t  w_;
  void     *arg_;
};





/** Just a message queue. 
    
    Race checker can not understand that message queue creates
    a happens-before relation between Put() and Get(). 
    We help race checker by creating a semaphore: 

    Sender:                                     Receiver: 
    1. sem_init(sem, 0, 0)                    
    2. sem_post(sem)       -----\              
    3. Put({item,sem})           \               
                                  \            a. {item,sem} = Get()
                                   \---------> b. sem_wait(sem)
                                               c. sem_destroy(sem)

    Here we actually create a semaphore and do post/wait(). 
    With helgrind the same effect could be achieved by calling user requests 
    _VG_USERREQ__HG_POSIX_SEM_* on a fake semaphore. 


    static long fake;
      Sender:                                    Receiver:
      1. AtomicIncrement(fake)                  
      2. HG_SEM_INIT_POST(fake)      
      3. HG_SEM_POST_PRE(fake) -------    
      4. Put({item,fake})             \           
                                       \         a. {item, sem} = Get()
                                        ------>  b. HG_SEM_WAIT_POST(sem)
                                                 c. HG_SEM_DESTROY_PRE(sem)


*/
class ProducerConsumerQueue {
 public:
  ProducerConsumerQueue(int unused) {}
  ~ProducerConsumerQueue() { CHECK(q_.empty()); }

  // Put. 
  void Put(void *item) {
    mu_.Lock();
      item_t *t = new item_t;
      t->item = item;
      sem_init(&t->sem, 0, 0);
      sem_post(&t->sem);
      q_.push(t);
    mu_.Unlock();
  }

  // Get. 
  // Spins if the queue is empty. 
  // Real implementation woudl probably block on CV, 
  // but this is irrelevant for the unit tests. 
  void *Get() {
    void *item = NULL;
    void *sem = NULL;
    while(true) {
      bool have_item = false;
      mu_.Lock();
      if (!q_.empty()) {
        item_t *t = q_.front();
        q_.pop();
        item       = t->item;
        sem_wait(&t->sem);
        sem_destroy(&t->sem); 
        delete t;
        have_item = true;
      }
      mu_.Unlock();
      if (have_item) {
        break;
      }
      usleep(1000); // don't burn CPU
    }
    return item;
  }

 private: 
  struct item_t{
    void  *item; 
    sem_t sem;
  };
  Mutex             mu_;
  std::queue<item_t*> q_; // protected by mu_
};



/// Function pointer with zero, one or two parameters. 
struct Closure {
  typedef void (*F0)();
  typedef void (*F1)(void *arg1);
  typedef void (*F2)(void *arg1, void *arg2);
  int  n_params; 
  void *f; 
  void *param1; 
  void *param2; 

  void Execute() {
    if (n_params == 0) {
      (F0(f))();
      return;
    }
    if (n_params == 1) {
      (F1(f))(param1);
      return;
    }
    CHECK(n_params == 2);
    (F2(f))(param1, param2);
  }
}; 

template <class T>
Closure *NewCallback(T f) {
  Closure *res = new Closure;
  res->n_params = 0;
  res->f = (void*)(f);
  res->param1 = NULL;
  res->param2 = NULL;
  return res;
}

template <class T>
Closure *NewCallback(T f, void *p1) {
  Closure *res = new Closure;
  res->n_params = 1;
  res->f = (void*)(f);
  res->param1 = p1;
  res->param2 = NULL;
  return res;
}


/*! A thread pool that uses ProducerConsumerQueue. 
  Usage: 
  {
    ThreadPool pool(n_workers);
    pool.Add(NewCallback(func_with_no_args));
    pool.Add(NewCallback(func_with_one_arg, arg));
    pool.Add(NewCallback(func_with_two_args, arg1, arg2));
    ... // more calls to pool.Add() 
    
    // the ~ThreadPool() is called: we wait workers to finish 
    // and then join all threads in the pool. 
  }
*/
class ThreadPool {
 public: 
  //! Create n_threads threads, but do not start. 
  explicit ThreadPool(int n_threads) 
    : queue_(INT_MAX) {
    for (int i = 0; i < n_threads; i++) {
      MyThread *thread = new MyThread(&ThreadPool::Worker);
      workers_.push_back(thread);
    }
  }

  //! Start all threads. 
  void StartWorkers() {
    for (int i = 0; i < workers_.size(); i++) {
      workers_[i]->Start(this);
    }
  }

  //! Add a closure. 
  void Add(Closure *closure) {
    queue_.Put(closure);
  }

  int num_threads() { return workers_.size();}

  //! Wait workers to finish, then join all threads.
  ~ThreadPool() {
    for (int i = 0; i < workers_.size(); i++) {
      Add(NULL);
    }
    for (int i = 0; i < workers_.size(); i++) {
      workers_[i]->Join();
    }
  }
 private:
  std::vector<MyThread*>   workers_;
  ProducerConsumerQueue  queue_;

  static void *Worker(void *p) {
    ThreadPool *pool = reinterpret_cast<ThreadPool*>(p);
    while (true) {
      Closure *closure = reinterpret_cast<Closure*>(pool->queue_.Get());
      if(closure == NULL) {
        return NULL; 
      }
      closure->Execute();     
    }
  }
};

#ifndef NO_BARRIER
/// Wrapper for pthread_barrier_t. 
class Barrier{
 public:
  explicit Barrier(int n_threads) {CHECK(0 == pthread_barrier_init(&b_, 0, n_threads));}
  ~Barrier()                      {CHECK(0 == pthread_barrier_destroy(&b_));}
  void Block() {
    // helgrind 3.3.0 does not have an interceptor for barrier.
    // but our current local version does. 
    // ANNOTATE_CONDVAR_SIGNAL(this);
    pthread_barrier_wait(&b_);
    // ANNOTATE_CONDVAR_WAIT(this, this);
  }
 private:
  pthread_barrier_t b_;
};

#endif // NO_BARRIER



#endif // THREAD_WRAPPERS_PTHREAD_H
// vim:shiftwidth=2:softtabstop=2:expandtab:foldmethod=marker
