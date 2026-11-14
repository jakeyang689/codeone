/* Platform random number generator abstraction */
/* macOS: arc4random_buf, Linux: getrandom */

#ifndef C1_RANDOM_H
#define C1_RANDOM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fill buf with len cryptographically secure random bytes */
void c1_random_buf(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* C1_RANDOM_H */
