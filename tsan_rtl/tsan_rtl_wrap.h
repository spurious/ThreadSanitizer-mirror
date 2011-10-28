/* Copyright (c) 2010-2011, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Author: glider@google.com (Alexander Potapenko)

#ifndef LLVM_SRC_MOP_IMPL_MOP_WRAP_H_
#define LLVM_SRC_MOP_IMPL_MOP_WRAP_H_

#include <pthread.h>
#include <semaphore.h>

// Real function prototypes {{{1
extern "C" {
void __real___libc_csu_init(void);

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

int __real_pthread_rwlock_init(pthread_rwlock_t *rwlock,
                               const pthread_rwlockattr_t *attr);
int __real_pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int __real_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int __real_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int __real_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int __real_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int __real_pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

int __real_pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int __real_pthread_spin_destroy(pthread_spinlock_t *lock);
int __real_pthread_spin_lock(pthread_spinlock_t *lock);
int __real_pthread_spin_trylock(pthread_spinlock_t *lock);
int __real_pthread_spin_unlock(pthread_spinlock_t *lock);

int __real_pthread_cond_signal(pthread_cond_t *cond);
int __real_pthread_cond_broadcast(pthread_cond_t *cond);
int __real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int __real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                  const struct timespec *abstime);

int __real_pthread_key_create(pthread_key_t *key,
                              void (*destr_function) (void *));

sem_t *__real_sem_open(const char *name, int oflag,
                mode_t mode, unsigned int value);
int __real_sem_wait(sem_t *sem);
int __real_sem_trywait(sem_t *sem);
int __real_sem_post(sem_t *sem);
int __real_sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);
int __real_sem_getvalue(sem_t *sem, int *value);

int __real_usleep(useconds_t usec);
int __real_nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int __real_sleep(unsigned int seconds);
int __real_clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);
int __real_sched_yield();
int __real_pthread_yield();

int __real___cxa_guard_acquire(int *guard);
int __real___cxa_guard_release(int *guard);

int __real_atexit(void (*function)(void));
void __real_exit(int status);

pid_t __real_fork();

size_t __real_strlen(const char *s);
int __real_strcmp(const char *s1, const char *s2);
void *__real_memchr(const char *s, int c, size_t n);
char *__real_memcpy(char *dest, const char *src, size_t n);
void *__real_memmove(void *dest, const void *src, size_t n);
char *__real_strchr(const char *s, int c);
char *__real_strchrnul(const char *s, int c);
char *__real_strrchr(const char *s, int c);
int __real_strncmp(const char *s1, const char *s2, size_t n);
char *__real_strcpy(char *dest, const char *src);

void *__real_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset);
int __real_munmap(void *addr, size_t length);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_malloc(size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);
int __real_posix_memalign(void **memptr, size_t alignment, size_t size);

ssize_t __real_write(int fd, const void *buf, size_t count);
ssize_t __real_read(int fd, const void *buf, size_t count);
int __real_lockf64(int fd, int cmd, off64_t len);

ssize_t __real_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t __real_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t __real_sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t __real_recvmsg(int sockfd, struct msghdr *msg, int flags);

int __real_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int __real_epoll_wait(int epfd, struct epoll_event *events,
                      int maxevents, int timeout);

int __real_pthread_once(pthread_once_t *once_control,
                        void (*init_routine) (void));

int __real_pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count);
int __real_pthread_barrier_wait(pthread_barrier_t *barrier);

int __real_sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact);


// operator new(unsigned int)
void *__real__Znwj(unsigned int size);
void *__real__ZnwjRKSt9nothrow_t(unsigned int size, nothrow_t &nt);
// operator new[](unsigned int)
void *__real__Znaj(unsigned int size);
void *__real__ZnajRKSt9nothrow_t(unsigned int size, nothrow_t &nt);
// operator new(unsigned long)
void *__real__Znwm(unsigned long size);
void *__real__ZnwmRKSt9nothrow_t(unsigned long size, nothrow_t &nt);
// operator new[](unsigned long)
void *__real__Znam(unsigned long size);
void *__real__ZnamRKSt9nothrow_t(unsigned long size, nothrow_t &nt);
// operator delete(void*)
void __real__ZdlPv(void *ptr);
void __real__ZdlPvRKSt9nothrow_t(void *ptr, nothrow_t &nt);
// operator delete[](void*)
void __real__ZdaPv(void *ptr);
void __real__ZdaPvRKSt9nothrow_t(void *ptr, nothrow_t &nt);
}
// }}}

// Wrapper prototypes {{{1
extern "C" {
void __wrap___libc_csu_init(void);

int __wrap_pthread_create(pthread_t *thread,
                          const pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg);
int __wrap_pthread_join(pthread_t thread, void **value_ptr);

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *mutexattr);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex);

int __wrap_pthread_rwlock_init(pthread_rwlock_t *rwlock,
                               const pthread_rwlockattr_t *attr);
int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int __wrap_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int __wrap_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int __wrap_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int __wrap_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

int __wrap_pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int __wrap_pthread_spin_destroy(pthread_spinlock_t *lock);
int __wrap_pthread_spin_lock(pthread_spinlock_t *lock);
int __wrap_pthread_spin_trylock(pthread_spinlock_t *lock);
int __wrap_pthread_spin_unlock(pthread_spinlock_t *lock);

int __wrap_pthread_cond_signal(pthread_cond_t *cond);
int __wrap_pthread_cond_broadcast(pthread_cond_t *cond);
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                  const struct timespec *abstime);

int __wrap_pthread_key_create(pthread_key_t *key,
                              void (*destr_function) (void *));

sem_t *__wrap_sem_open(const char *name, int oflag,
                mode_t mode, unsigned int value);
int __wrap_sem_wait(sem_t *sem);
int __wrap_sem_trywait(sem_t *sem);
int __wrap_sem_post(sem_t *sem);
int __wrap_sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);
int __wrap_sem_getvalue(sem_t *sem, int *value);

int __wrap_usleep(useconds_t usec);
int __wrap_nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int __wrap_sleep(unsigned int seconds);
int __wrap_clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);
int __wrap_sched_yield();
int __wrap_pthread_yield();

int __wrap___cxa_guard_acquire(int *guard);
int __wrap___cxa_guard_release(int *guard);

int __wrap_atexit(void (*function)(void));
void __wrap_exit(int status);

pid_t __wrap_fork();

size_t __wrap_strlen(const char *s);
int __wrap_strcmp(const char *s1, const char *s2);
void *__wrap_memchr(const char *s, int c, size_t n);
char *__wrap_memcpy(char *dest, const char *src, size_t n);
void *__wrap_memmove(void *dest, const void *src, size_t n);
char *__wrap_strchr(const char *s, int c);
char *__wrap_strchrnul(const char *s, int c);
char *__wrap_strrchr(const char *s, int c);
int __wrap_strncmp(const char *s1, const char *s2, size_t n);
char *__wrap_strcpy(char *dest, const char *src);

void *__wrap_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset);
int __wrap_munmap(void *addr, size_t length);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_realloc(void *ptr, size_t size);
void __wrap_free(void *ptr);
int __wrap_posix_memalign(void **memptr, size_t alignment, size_t size);

ssize_t __wrap_write(int fd, const void *buf, size_t count);
ssize_t __wrap_read(int fd, const void *buf, size_t count);
int __wrap_lockf64(int fd, int cmd, off64_t len);

ssize_t __wrap_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t __wrap_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t __wrap_sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t __wrap_recvmsg(int sockfd, struct msghdr *msg, int flags);

int __wrap_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int __wrap_epoll_wait(int epfd, struct epoll_event *events,
                      int maxevents, int timeout);

int __wrap_pthread_once(pthread_once_t *once_control,
                        void (*init_routine) (void));

int __wrap_pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count);
int __wrap_pthread_barrier_wait(pthread_barrier_t *barrier);

int __wrap_sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact);


// operator new(unsigned int)
void *__wrap__Znwj(unsigned int size);
void *__wrap__ZnwjRKSt9nothrow_t(unsigned int size, nothrow_t &nt);
// operator new[](unsigned int)
void *__wrap__Znaj(unsigned int size);
void *__wrap__ZnajRKSt9nothrow_t(unsigned int size, nothrow_t &nt);
// operator new(unsigned long)
void *__wrap__Znwm(unsigned long size);
void *__wrap__ZnwmRKSt9nothrow_t(unsigned long size, nothrow_t &nt);
// operator new[](unsigned long)
void *__wrap__Znam(unsigned long size);
void *__wrap__ZnamRKSt9nothrow_t(unsigned long size, nothrow_t &nt);
// operator delete(void*)
void __wrap__ZdlPv(void *ptr);
void __wrap__ZdlPvRKSt9nothrow_t(void *ptr, nothrow_t &nt);
// operator delete[](void*)
void __wrap__ZdaPv(void *ptr);
void __wrap__ZdaPvRKSt9nothrow_t(void *ptr, nothrow_t &nt);
}
// }}}
#endif  // LLVM_SRC_MOP_IMPL_MOP_WRAP_H_
