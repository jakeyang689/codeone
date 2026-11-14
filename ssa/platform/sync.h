/* Platform synchronization primitives — unified API for SSA extern calls */

#ifndef C1_SYNC_H
#define C1_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct c1_mutex c1_mutex;
typedef struct c1_cond c1_cond;

/* Mutex. lock/unlock/destroy return 0 on success, -1 on failure.
 * trylock returns 0 if acquired, 1 if already held, -1 on failure. */
c1_mutex *c1_mutex_create(void);
int c1_mutex_lock(c1_mutex *m);
int c1_mutex_trylock(c1_mutex *m);
int c1_mutex_unlock(c1_mutex *m);
int c1_mutex_destroy(c1_mutex *m);

/* Condition variable: wait atomically releases the mutex and reacquires it.
 * wait returns 0 on success, -1 on failure.
 * timedwait returns 0 if signalled, 1 on timeout, -1 on failure. */
c1_cond *c1_cond_create(void);
int c1_cond_wait(c1_cond *c, c1_mutex *m);
int c1_cond_timedwait(c1_cond *c, c1_mutex *m, int ms);
int c1_cond_signal(c1_cond *c);
int c1_cond_broadcast(c1_cond *c);
int c1_cond_destroy(c1_cond *c);

/* Scheduling helpers */
void c1_thread_yield(void);
void c1_thread_sleep_ms(int ms);
long long c1_thread_self(void);
int c1_cpu_count(void);

#ifdef __cplusplus
}
#endif

#endif /* C1_SYNC_H */
