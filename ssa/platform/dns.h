/* DNS resolution wrapper for SSA extern calls */
#ifndef C1_DNS_H
#define C1_DNS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve hostname to IPv4 dotted-quad string.
 * Returns 0 on success, -1 on error.
 * addr_buf: output buffer (at least 16 bytes). */
int c1_dns_resolve(const char *hostname, char *addr_buf, int addr_buf_len);

#ifdef __cplusplus
}
#endif

#endif /* C1_DNS_H */
