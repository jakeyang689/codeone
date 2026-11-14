/* Platform I/O multiplexing — epoll (Linux) / kqueue (macOS) */

#include "ioselect.h"
#include <stdlib.h>
#include <errno.h>

#ifdef __APPLE__
  #include <sys/types.h>
  #include <sys/event.h>
  #include <sys/time.h>
  #include <unistd.h>

int c1_ioselect_create(void) {
    int kq = kqueue();
    if (kq < 0) return -errno;
    return kq;
}

int c1_ioselect_add(int ioselect_fd, int fd, int events, int ident) {
    struct kevent kev[2];
    int n = 0;
    if (events & 1) {
        EV_SET(&kev[n++], (uintptr_t)fd, EVFILT_READ, EV_ADD, 0, 0, (void *)(intptr_t)ident);
    }
    if (events & 2) {
        EV_SET(&kev[n++], (uintptr_t)fd, EVFILT_WRITE, EV_ADD, 0, 0, (void *)(intptr_t)ident);
    }
    if (kevent(ioselect_fd, kev, n, NULL, 0, NULL) < 0)
        return -errno;
    return 0;
}

int c1_ioselect_del(int ioselect_fd, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    /* Ignore errors on delete — fd may only have one filter registered */
    (void)kevent(ioselect_fd, kev, 2, NULL, 0, NULL);
    return 0;
}

int c1_ioselect_wait(int ioselect_fd, c1_ioevent *events, int max, int timeout_ms) {
    struct kevent *kev = (struct kevent *)calloc((size_t)max, sizeof(struct kevent));
    if (!kev) return -ENOMEM;

    struct timespec ts, *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }

    int n = (int)kevent(ioselect_fd, NULL, 0, kev, max, tsp);
    if (n < 0) { free(kev); return -errno; }

    for (int i = 0; i < n; i++) {
        events[i].ident = (int)(intptr_t)kev[i].udata;
        events[i].fd = (int)kev[i].ident;
        events[i].events = 0;
        if (kev[i].filter == EVFILT_READ)  events[i].events |= 1;
        if (kev[i].filter == EVFILT_WRITE) events[i].events |= 2;
    }
    free(kev);
    return n;
}

int c1_ioselect_close(int ioselect_fd) {
    if (close(ioselect_fd) < 0) return -errno;
    return 0;
}

#elif __linux__
  #include <sys/epoll.h>
  #include <unistd.h>

int c1_ioselect_create(void) {
    int epfd = epoll_create1(0);
    if (epfd < 0) return -errno;
    return epfd;
}

int c1_ioselect_add(int ioselect_fd, int fd, int events, int ident) {
    struct epoll_event ev;
    ev.events = 0;
    if (events & 1) ev.events |= EPOLLIN;
    if (events & 2) ev.events |= EPOLLOUT;
    ev.data.u64 = (uint64_t)(unsigned int)ident;
    if (epoll_ctl(ioselect_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        return -errno;
    return 0;
}

int c1_ioselect_del(int ioselect_fd, int fd) {
    if (epoll_ctl(ioselect_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
        return -errno;
    return 0;
}

int c1_ioselect_wait(int ioselect_fd, c1_ioevent *events, int max, int timeout_ms) {
    struct epoll_event *epev = (struct epoll_event *)calloc((size_t)max, sizeof(struct epoll_event));
    if (!epev) return -ENOMEM;

    int n = epoll_wait(ioselect_fd, epev, max, timeout_ms);
    if (n < 0) { free(epev); return -errno; }

    for (int i = 0; i < n; i++) {
        events[i].ident = (int)epev[i].data.u64;
        events[i].fd = -1; /* epoll doesn't directly expose fd in wait */
        events[i].events = 0;
        if (epev[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) events[i].events |= 1;
        if (epev[i].events & EPOLLOUT) events[i].events |= 2;
    }
    free(epev);
    return n;
}

int c1_ioselect_close(int ioselect_fd) {
    if (close(ioselect_fd) < 0) return -errno;
    return 0;
}

#else
  #error "Unsupported platform"
#endif

/* Flat-buffer variant: wraps c1_ioselect_wait and writes [ident,fd,events] triples */
int c1_ioselect_wait_flat(int ioselect_fd, int *flat, int max, int timeout_ms) {
    c1_ioevent *evs = (c1_ioevent *)calloc((size_t)max, sizeof(c1_ioevent));
    if (!evs) return -ENOMEM;
    int n = c1_ioselect_wait(ioselect_fd, evs, max, timeout_ms);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            flat[i * 3 + 0] = evs[i].ident;
            flat[i * 3 + 1] = evs[i].fd;
            flat[i * 3 + 2] = evs[i].events;
        }
    }
    free(evs);
    return n;
}
