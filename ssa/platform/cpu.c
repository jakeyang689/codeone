/* Hardware crypto feature detection.
   macOS arm64: sysctl hw.optional.arm.FEAT_*
   Linux arm64: getauxval(AT_HWCAP)
   x86 (both):  cpuid

   Set C1_NO_HW=1 to force the feature mask to zero, which makes the std
   library take its software paths. Used to verify the fallback and to
   benchmark both paths on the same machine. */

#include "cpu.h"

#include <stdlib.h>

#if defined(__APPLE__) && defined(__aarch64__)

  #include <sys/sysctl.h>

static int sysctl_flag(const char *name) {
    int v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0) return 0;
    return v != 0;
}

static uint64_t detect(void) {
    uint64_t f = 0;
    if (sysctl_flag("hw.optional.arm.FEAT_AES"))    f |= C1_CPU_AES;
    if (sysctl_flag("hw.optional.arm.FEAT_SHA256")) f |= C1_CPU_SHA256;
    if (sysctl_flag("hw.optional.arm.FEAT_PMULL"))  f |= C1_CPU_PMULL;
    if (sysctl_flag("hw.optional.arm.FEAT_SHA512")) f |= C1_CPU_SHA512;
    return f;
}

#elif defined(__linux__) && defined(__aarch64__)

  #include <sys/auxv.h>
  #include <asm/hwcap.h>

static uint64_t detect(void) {
    unsigned long hwc = getauxval(AT_HWCAP);
    uint64_t f = 0;
  #ifdef HWCAP_AES
    if (hwc & HWCAP_AES)    f |= C1_CPU_AES;
  #endif
  #ifdef HWCAP_SHA2
    if (hwc & HWCAP_SHA2)   f |= C1_CPU_SHA256;
  #endif
  #ifdef HWCAP_PMULL
    if (hwc & HWCAP_PMULL)  f |= C1_CPU_PMULL;
  #endif
  #ifdef HWCAP_SHA512
    if (hwc & HWCAP_SHA512) f |= C1_CPU_SHA512;
  #endif
    return f;
}

#elif defined(__x86_64__) || defined(__i386__)

  #include <cpuid.h>

static uint64_t detect(void) {
    unsigned a, b, c, d;
    uint64_t f = 0;
    if (__get_cpuid(1, &a, &b, &c, &d)) {
        if (c & (1u << 25)) f |= C1_CPU_AES;    /* AES-NI */
        if (c & (1u << 1))  f |= C1_CPU_PMULL;  /* PCLMULQDQ */
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        if (b & (1u << 29)) f |= C1_CPU_SHA256; /* SHA extensions */
    }
    return f;
}

#else

static uint64_t detect(void) { return 0; }

#endif

uint64_t c1_cpu_features(void) {
    static uint64_t cached;
    static int probed;
    if (probed) return cached;
    probed = 1;
    cached = detect();
    const char *force = getenv("C1_NO_HW");
    if (force && force[0] == '1') cached = 0;
    return cached;
}
