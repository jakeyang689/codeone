/* Secure memory helpers — unified API for SSA extern calls */

#ifndef C1_SECURE_H
#define C1_SECURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zero n bytes through a volatile pointer so the compiler cannot elide the
 * stores when the buffer is no longer read. */
void c1_secure_zero(void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* C1_SECURE_H */
