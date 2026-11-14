/* Platform time: macOS and Linux (POSIX clock_gettime) */

#if defined(__APPLE__)
  #define _DARWIN_C_SOURCE 1
#elif defined(__linux__)
  #define _POSIX_C_SOURCE 200809L
#else
  #error "Unsupported platform"
#endif

#include "time.h"

#include <time.h>

static long long to_ms(struct timespec ts) {
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

long long c1_time_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return to_ms(ts);
}

long long c1_time_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return to_ms(ts);
}
