#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void    gcctsan_enter                 (void const volatile*);
void    gcctsan_leave                 ();
void    gcctsan_store                 (void const volatile*);
void    gcctsan_load                  (void const volatile*);

int     gcctsan_pthread_create        (pthread_t*,
                                       pthread_attr_t*,
                                       void *(*start_routine)(void*),
                                       void *arg);

int     gcctsan_pthread_join          (pthread_t, void**);

int     gcctsan_pthread_mutex_init    (pthread_mutex_t*, pthread_mutexattr_t*);
int     gcctsan_pthread_mutex_destroy (pthread_mutex_t*);
int     gcctsan_pthread_mutex_lock    (pthread_mutex_t*);
int     gcctsan_pthread_mutex_unlock  (pthread_mutex_t*);

int     gcctsan_pthread_cond_init     (pthread_cond_t*,
                                       pthread_condattr_t const*);
int     gcctsan_pthread_cond_destroy  (pthread_cond_t*);
int     gcctsan_pthread_cond_signal   (pthread_cond_t*);
int     gcctsan_pthread_cond_broadcast(pthread_cond_t*);
int     gcctsan_pthread_cond_wait     (pthread_cond_t*,
                                       pthread_mutex_t*);
int     gcctsan_pthread_cond_timedwait(pthread_cond_t*,
                                       pthread_mutex_t*,
                                       struct timespec const*);

#ifdef __cplusplus
}
#endif

