/* Platform synchronization: macOS and Linux (POSIX pthreads) */

#if defined(__APPLE__)
  #define _DARWIN_C_SOURCE 1
#elif defined(__linux__)
  #define _POSIX_C_SOURCE 200809L
#else
  #error "Unsupported platform"
#endif

#include "sync.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
  #include <sys/sysctl.h>
#endif

struct c1_mutex {
    pthread_mutex_t handle;
};

struct c1_cond {
    pthread_cond_t handle;
};

c1_mutex *c1_mutex_create(void) {
    c1_mutex *m = (c1_mutex *)malloc(sizeof(*m));
    if (!m) return NULL;
    if (pthread_mutex_init(&m->handle, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

int c1_mutex_lock(c1_mutex *m) {
    if (!m) return -1;
    return pthread_mutex_lock(&m->handle) == 0 ? 0 : -1;
}

int c1_mutex_trylock(c1_mutex *m) {
    if (!m) return -1;
    int rc = pthread_mutex_trylock(&m->handle);
    if (rc == 0) return 0;
    return rc == EBUSY ? 1 : -1;
}

int c1_mutex_unlock(c1_mutex *m) {
    if (!m) return -1;
    return pthread_mutex_unlock(&m->handle) == 0 ? 0 : -1;
}

int c1_mutex_destroy(c1_mutex *m) {
    if (!m) return -1;
    int rc = pthread_mutex_destroy(&m->handle);
    free(m);
    return rc == 0 ? 0 : -1;
}

c1_cond *c1_cond_create(void) {
    c1_cond *c = (c1_cond *)malloc(sizeof(*c));
    if (!c) return NULL;
    if (pthread_cond_init(&c->handle, NULL) != 0) {
        free(c);
        return NULL;
    }
    return c;
}

int c1_cond_wait(c1_cond *c, c1_mutex *m) {
    if (!c || !m) return -1;
    return pthread_cond_wait(&c->handle, &m->handle) == 0 ? 0 : -1;
}

int c1_cond_timedwait(c1_cond *c, c1_mutex *m, int ms) {
    if (!c || !m || ms < 0) return -1;

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    int rc = pthread_cond_timedwait(&c->handle, &m->handle, &ts);
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? 1 : -1;
}

int c1_cond_signal(c1_cond *c) {
    if (!c) return -1;
    return pthread_cond_signal(&c->handle) == 0 ? 0 : -1;
}

int c1_cond_broadcast(c1_cond *c) {
    if (!c) return -1;
    return pthread_cond_broadcast(&c->handle) == 0 ? 0 : -1;
}

int c1_cond_destroy(c1_cond *c) {
    if (!c) return -1;
    int rc = pthread_cond_destroy(&c->handle);
    free(c);
    return rc == 0 ? 0 : -1;
}

void c1_thread_yield(void) {
    sched_yield();
}

void c1_thread_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

long long c1_thread_self(void) {
    return (long long)(uintptr_t)pthread_self();
}

int c1_cpu_count(void) {
#if defined(__APPLE__)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.ncpu", &n, &len, NULL, 0) != 0) return 1;
    return n > 0 ? n : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}
