#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void    relite_enter                  (void const volatile*);
void    relite_leave                  ();
void    relite_store                  (void const volatile*);
void    relite_load                   (void const volatile*);

int     relite_pthread_create         (pthread_t*,
                                       pthread_attr_t*,
                                       void *(*start_routine)(void*),
                                       void *arg);

int     relite_pthread_join           (pthread_t, void**);

int     relite_pthread_mutex_init     (pthread_mutex_t*,
                                       pthread_mutexattr_t*);
int     relite_pthread_mutex_destroy  (pthread_mutex_t*);
int     relite_pthread_mutex_lock     (pthread_mutex_t*);
int     relite_pthread_mutex_unlock   (pthread_mutex_t*);

int     relite_pthread_cond_init      (pthread_cond_t*,
                                       pthread_condattr_t const*);
int     relite_pthread_cond_destroy   (pthread_cond_t*);
int     relite_pthread_cond_signal    (pthread_cond_t*);
int     relite_pthread_cond_broadcast (pthread_cond_t*);
int     relite_pthread_cond_wait      (pthread_cond_t*,
                                       pthread_mutex_t*);
int     relite_pthread_cond_timedwait (pthread_cond_t*,
                                       pthread_mutex_t*,
                                       struct timespec const*);

#ifdef __cplusplus
}
#endif


