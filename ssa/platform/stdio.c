/* Platform standard-stream handles (ISO C) */

#include "stdio.h"

#include <stdio.h>

void *c1_stdio_stdin(void)  { return (void *)stdin; }
void *c1_stdio_stdout(void) { return (void *)stdout; }
void *c1_stdio_stderr(void) { return (void *)stderr; }
