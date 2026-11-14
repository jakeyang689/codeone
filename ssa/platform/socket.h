/* Platform socket abstraction layer — unified API for SSA extern calls */

#ifndef C1_SOCKET_H
#define C1_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Socket creation */
int c1_socket_create(int domain, int type, int protocol);

/* Server operations */
int c1_socket_bind(int fd, int port);
int c1_socket_listen(int fd, int backlog);
int c1_socket_accept(int fd);

/* Client operations */
int c1_socket_connect(int fd, const char *ip, int port);
int c1_socket_connect_loopback(int fd, int port);

/* Data transfer */
int c1_socket_send(int fd, const char *buf, int len);
int c1_socket_recv(int fd, char *buf, int len);

/* Close */
int c1_socket_close(int fd);

/* Utility */
int c1_socket_set_nonblock(int fd);

/* Convenience: parse dotted IPv4 string to uint32 */
unsigned int c1_ipv4(const char *ip);

#ifdef __cplusplus
}
#endif

#endif /* C1_SOCKET_H */
