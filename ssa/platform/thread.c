/* Platform threads: macOS and Linux (POSIX pthreads) */

#if defined(__APPLE__) || defined(__linux__)
  #define _POSIX_C_SOURCE 200809L
#else
  #error "Unsupported platform"
#endif

#include "thread.h"

#include <pthread.h>
#include <stdlib.h>

/* Provided by boxes/std/thread/thread.bx. */
extern int c1_thread_entry(void *task);

/* Must match struct @_ThreadTask in boxes/std/thread/thread.bx. */
typedef struct {
    int task_id;
    void *arg;
} c1_task;

struct c1_thread {
    pthread_t handle;
};

static void *c1_thread_trampoline(void *p) {
    c1_task *task = (c1_task *)p;
    c1_thread_entry(task);
    free(task);
    return NULL;
}

c1_thread *c1_thread_create(int task_id, void *arg) {
    c1_task *task = (c1_task *)malloc(sizeof(*task));
    if (!task) return NULL;
    task->task_id = task_id;
    task->arg = arg;

    c1_thread *t = (c1_thread *)malloc(sizeof(*t));
    if (!t) {
        free(task);
        return NULL;
    }

    if (pthread_create(&t->handle, NULL, c1_thread_trampoline, task) != 0) {
        free(task);
        free(t);
        return NULL;
    }
    return t;
}

int c1_thread_join(c1_thread *t) {
    if (!t) return -1;
    int rc = pthread_join(t->handle, NULL);
    free(t);
    return rc == 0 ? 0 : -1;
}

int c1_thread_detach(c1_thread *t) {
    if (!t) return -1;
    int rc = pthread_detach(t->handle);
    free(t);
    return rc == 0 ? 0 : -1;
}
