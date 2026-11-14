#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* Bitmask describing hardware crypto extensions present on the running CPU. */
#define C1_CPU_AES    (1ULL << 0)
#define C1_CPU_SHA256 (1ULL << 1)
#define C1_CPU_PMULL  (1ULL << 2)
#define C1_CPU_SHA512 (1ULL << 3)

/* Detected once and cached; returns 0 on unsupported platforms. */
uint64_t c1_cpu_features(void);

#endif /* CPU_H */
