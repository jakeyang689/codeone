#!/usr/bin/env python3
"""Minimal TLS server for the TLS client tests.

Serves one HTTP response over a TLS connection using certs/tls13.pem unless
a different certificate/key pair is given on the command line.
Usage: python3 tls13_server.py [port [maxver [cert [key]]]]
"""

import os
import socket
import ssl
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8801
MAXVER = sys.argv[2] if len(sys.argv) > 2 else "1.3"
HERE = os.path.dirname(os.path.abspath(__file__))
CERT = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "certs", "tls13.pem")
KEY = sys.argv[4] if len(sys.argv) > 4 else os.path.join(HERE, "certs", "tls13.key")


def main():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    if MAXVER == "1.2":
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
        # The c1 TLS 1.2 client implements RSA key exchange with
        # AES-128-GCM; OpenSSL does not offer that suite by default.
        ctx.set_ciphers("AES128-GCM-SHA256:@SECLEVEL=0")
    else:
        ctx.minimum_version = ssl.TLSVersion.TLSv1_3
        ctx.maximum_version = ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(CERT, KEY)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(5)
    try:
        # The client first attempts TLS 1.3 and reconnects for TLS 1.2 when
        # that version is not supported: keep accepting until one connection
        # completes the HTTP exchange.
        for _ in range(5):
            conn, _ = srv.accept()
            conn.settimeout(10)
            try:
                ss = ctx.wrap_socket(conn, server_side=True)
                data = b""
                while b"\r\n\r\n" not in data:
                    chunk = ss.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                ss.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"
                           b"Connection: close\r\n\r\nhello")
                try:
                    ss.unwrap()
                except Exception:
                    pass
                ss.close()
                break
            except Exception as exc:
                sys.stderr.write("tls13_server[%s]: %r\n" % (MAXVER, exc))
                try:
                    conn.close()
                except Exception:
                    pass
                continue
    finally:
        srv.close()


if __name__ == "__main__":
    main()
