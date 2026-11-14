/* Platform time abstraction — unified API for SSA extern calls */

#ifndef C1_TIME_H
#define C1_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Wall-clock time in milliseconds since the Unix epoch (UTC). */
long long c1_time_now_ms(void);

/* Monotonic time in milliseconds; suitable for measuring elapsed time. */
long long c1_time_monotonic_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* C1_TIME_H */
