/* DNS resolution wrapper — getaddrinfo for SSA extern calls */
/* macOS: #include <sys/types.h> + <sys/socket.h> + <netdb.h> + <arpa/inet.h> */
/* Linux: #include <sys/socket.h> + <netdb.h> + <arpa/inet.h> */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

/* Resolve hostname to IPv4 dotted-quad string.
 * Returns 0 on success, -1 on error.
 * addr_buf: output buffer (at least 16 bytes for "xxx.xxx.xxx.xxx\0"). */
int c1_dns_resolve(const char *hostname, char *addr_buf, int addr_buf_len) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    int ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret != 0 || result == NULL)
        return -1;

    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    const char *s = inet_ntop(AF_INET, &addr->sin_addr, addr_buf, (socklen_t)addr_buf_len);
    freeaddrinfo(result);

    return s ? 0 : -1;
}
