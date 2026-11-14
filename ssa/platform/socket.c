/* Platform socket abstraction — unified C API for SSA extern calls */

#include "socket.h"
#include <string.h>
#include <errno.h>

#ifdef __APPLE__
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <unistd.h>
  #define PLAT_SEND_FLAGS 0
  #define SOCKET_ERRNO errno
#elif __linux__
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <unistd.h>
  #define PLAT_SEND_FLAGS MSG_NOSIGNAL
  #define SOCKET_ERRNO errno
#else
  #error "Unsupported platform"
#endif

int c1_socket_create(int domain, int type, int protocol) {
    int fd;
#ifdef SOCK_CLOEXEC
    fd = (int)socket(domain, type | SOCK_CLOEXEC, protocol);
#else
    fd = (int)socket(domain, type, protocol);
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    if (fd < 0) return -SOCKET_ERRNO;

#ifdef __APPLE__
    /* On macOS, suppress SIGPIPE via socket option (Linux uses MSG_NOSIGNAL) */
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif

    return fd;
}

int c1_socket_bind(int fd, int port) {
    struct sockaddr_in addr;
    int one = 1;
    /* Let a listening socket rebind while old connections sit in TIME_WAIT. */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

int c1_socket_listen(int fd, int backlog) {
    if (listen(fd, backlog) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

int c1_socket_accept(int fd) {
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cfd = accept(fd, (struct sockaddr *)&cli, &len);
    if (cfd < 0) return -SOCKET_ERRNO;
    return cfd;
}

int c1_socket_connect(int fd, const char *ip, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

int c1_socket_connect_loopback(int fd, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

int c1_socket_send(int fd, const char *buf, int len) {
    int r = (int)send(fd, buf, (size_t)len, PLAT_SEND_FLAGS);
    if (r < 0) return -SOCKET_ERRNO;
    return r;
}

int c1_socket_recv(int fd, char *buf, int len) {
    int r = (int)recv(fd, buf, (size_t)len, 0);
    if (r < 0) return -SOCKET_ERRNO;
    return r;
}

int c1_socket_close(int fd) {
    if (close(fd) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

int c1_socket_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -SOCKET_ERRNO;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -SOCKET_ERRNO;
    return 0;
}

unsigned int c1_ipv4(const char *ip) {
    return inet_addr(ip);
}
