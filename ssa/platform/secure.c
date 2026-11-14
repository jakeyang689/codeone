/* Secure zeroing that survives dead-store elimination. */

#include "secure.h"

void c1_secure_zero(void *p, size_t n) {
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n > 0) {
        *v++ = 0;
        n--;
    }
}
