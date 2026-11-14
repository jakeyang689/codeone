/* Platform I/O multiplexing abstraction — unified API for SSA extern calls */

#ifndef C1_IOSELECT_H
#define C1_IOSELECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Event structure returned by c1_ioselect_wait */
typedef struct {
    int ident;      /* user-supplied identifier passed to c1_ioselect_add */
    int fd;         /* the actual file descriptor that triggered */
    int events;     /* bitmask: 1=readable, 2=writable */
} c1_ioevent;

/* Create an I/O multiplexer instance. Returns fd (>0) or negative errno. */
int c1_ioselect_create(void);

/* Register an fd with the multiplexer.
   events: 1=read, 2=write, 3=read|write.
   ident: opaque user value returned in c1_ioevent.ident (e.g. session id). */
int c1_ioselect_add(int ioselect_fd, int fd, int events, int ident);

/* Remove an fd from the multiplexer. */
int c1_ioselect_del(int ioselect_fd, int fd);

/* Wait for events. Returns number of events (0=timeout), or negative errno.
   max: capacity of events array.
   timeout_ms: -1 = infinite, 0 = no wait, >0 = milliseconds. */
int c1_ioselect_wait(int ioselect_fd, c1_ioevent *events, int max, int timeout_ms);

/* Flat-buffer variant: packs results as [ident, fd, events] int triples.
   Returns count of events. flat must hold at least max * 3 ints. */
int c1_ioselect_wait_flat(int ioselect_fd, int *flat, int max, int timeout_ms);

/* Close the multiplexer instance. */
int c1_ioselect_close(int ioselect_fd);

#ifdef __cplusplus
}
#endif

#endif /* C1_IOSELECT_H */
