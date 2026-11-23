/* <pthread.h> for the MSVC port: the subset used by `an-thread-pthreads.h`
 and `thread-specific.inc`, over SRW locks, `InitOnce` and thread-local
 storage slots (see `an_msvc_compat.c`). */
#ifndef AN_MSVC_PTHREAD_H
#define AN_MSVC_PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Each of these has the size and the all-zero initial state of the Windows
 object it stands for (`SRWLOCK`, `INIT_ONCE`). */
typedef struct { void* srwlock; } pthread_mutex_t;
typedef struct { void* init_once; } pthread_once_t;
typedef unsigned long pthread_key_t;

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_ONCE_INIT {0}

int pthread_once(pthread_once_t* once, void (*init)(void));

int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

#ifdef __cplusplus
}
#endif

#endif
