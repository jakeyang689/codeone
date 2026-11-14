/* Platform standard-stream handles — unified API for SSA extern calls */

#ifndef C1_STDIO_H
#define C1_STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return the C FILE* for the process standard streams. */
void *c1_stdio_stdin(void);
void *c1_stdio_stdout(void);
void *c1_stdio_stderr(void);

#ifdef __cplusplus
}
#endif

#endif /* C1_STDIO_H */
