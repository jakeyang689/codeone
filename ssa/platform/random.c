/* Platform random number generator */
/* macOS: arc4random_buf (stdlib.h, always available) */
/* Linux: getrandom (sys/random.h, since kernel 3.17) */

#include "random.h"

#ifdef __APPLE__
  #include <stdlib.h>
  void c1_random_buf(void *buf, size_t len) {
      arc4random_buf(buf, len);
  }
#elif __linux__
  #include <sys/random.h>
  #include <stddef.h>
  void c1_random_buf(void *buf, size_t len) {
      size_t offset = 0;
      while (offset < len) {
          ssize_t r = getrandom((char *)buf + offset, len - offset, 0);
          if (r > 0) offset += (size_t)r;
      }
  }
#else
  #error "Unsupported platform"
#endif
