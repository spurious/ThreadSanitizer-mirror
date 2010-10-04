#!/bin/bash
#
# Configure the LDFLAGS for linker scripts.

wrap () {
  LDFLAGS+="-Wl,--wrap,$1 "
}

LDFLAGS="-lpthread "

wrap __cxa_guard_acquire
wrap __cxa_guard_release

wrap pthread_create
wrap pthread_join

wrap pthread_mutex_init
wrap pthread_mutex_destroy
wrap pthread_mutex_lock
wrap pthread_mutex_unlock
wrap pthread_mutex_trylock

wrap pthread_spin_init
wrap pthread_spin_destroy
wrap pthread_spin_lock
wrap pthread_spin_trylock
wrap pthread_spin_unlock

wrap pthread_cond_signal
wrap pthread_cond_wait
wrap pthread_cond_timedwait

wrap atexit
wrap exit

wrap strlen
