#!/usr/bin/env python3
"""TLS 1.2 client for tls_server_test.bx. Exits 0 when the exchange works."""

import socket
import ssl
import sys

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
ctx.minimum_version = ssl.TLSVersion.TLSv1_2
ctx.maximum_version = ssl.TLSVersion.TLSv1_2
# The server profile is RSA key exchange; Python's default list omits it.
ctx.set_ciphers("AES128-GCM-SHA256")

try:
    with socket.create_connection(("127.0.0.1", 9443), timeout=10) as sock:
        with ctx.wrap_socket(sock, server_hostname="localhost") as tls:
            tls.sendall(b"hello tls server\n")
            data = tls.recv(64)
            if data != b"tls-ok\n":
                print(f"unexpected reply: {data!r}", file=sys.stderr)
                sys.exit(1)
            print(f"tls version: {tls.version()}")
except Exception as exc:  # noqa: BLE001 - report any handshake failure
    print(f"tls client failed: {exc}", file=sys.stderr)
    sys.exit(1)
