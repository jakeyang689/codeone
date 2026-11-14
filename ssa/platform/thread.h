/* Platform thread abstraction — unified API for SSA extern calls */

#ifndef C1_THREAD_H
#define C1_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thread handles are opaque. The entry point c1_thread_entry(void *task) is
 * provided by boxes/std/thread/thread.bx; the platform trampoline calls it with
 * a task record holding the task id and argument, then frees the record.
 */
typedef struct c1_thread c1_thread;

/* Create a thread running c1_thread_entry with task_id and arg.
 * Returns an opaque handle, or NULL on failure. */
c1_thread *c1_thread_create(int task_id, void *arg);

/* Join the thread and free the handle. Returns 0 or -1. */
int c1_thread_join(c1_thread *t);

/* Detach the thread and free the handle. Returns 0 or -1. */
int c1_thread_detach(c1_thread *t);

#ifdef __cplusplus
}
#endif

#endif /* C1_THREAD_H */
