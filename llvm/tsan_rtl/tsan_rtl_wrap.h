// Copyright 2010 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#ifndef LLVM_SRC_MOP_IMPL_MOP_WRAP_H_
#define LLVM_SRC_MOP_IMPL_MOP_WRAP_H_
extern "C" {
int __real_pthread_create(pthread_t *thread,
                          const pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg);
int __real_pthread_join(pthread_t thread, void **value_ptr);

int __real_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *mutexattr);
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __real_pthread_mutex_trylock(pthread_mutex_t *mutex);
int __real_pthread_mutex_unlock(pthread_mutex_t *mutex);
int __real_pthread_mutex_destroy(pthread_mutex_t *mutex);

int __real_pthread_spin_destroy(pthread_spinlock_t *lock);
int __real_pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int __real_pthread_spin_lock(pthread_spinlock_t *lock);
int __real_pthread_spin_trylock(pthread_spinlock_t *lock);
int __real_pthread_spin_unlock(pthread_spinlock_t *lock);

int __real_pthread_cond_signal(pthread_cond_t *cond);
int __real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int __real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                  const struct timespec *abstime);

int __real_sem_wait(sem_t *sem);
int __real_sem_trywait(sem_t *sem);
int __real_sem_post(sem_t *sem);

size_t __real_strlen(const char *s);
void *__real_memchr(const char *s, int c, size_t n);

int __real___cxa_guard_acquire(int *guard);
int __real___cxa_guard_release(int *guard);

int __real_atexit(void (*function)(void));
void __real_exit(int status);
}


#endif  // LLVM_SRC_MOP_IMPL_MOP_WRAP_H_
