#!/usr/bin/env python3
"""Minimal WebSocket echo server used by ws_echo_test.bx.

Speaks RFC 6455 with the standard library only:
  text "frag" -> fragmented text reply with a ping between fragments
                 (the final fragment is only sent after the pong arrives)
  text ...    -> "echo:" + text
  binary ...  -> echoed unchanged
  close       -> close echoed, connection closed

Usage: python3 ws_server.py [port]
"""

import base64
import hashlib
import socket
import struct
import sys

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class Conn:
    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def read_until(self, sep):
        while sep not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        end = self.buf.index(sep) + len(sep)
        data, self.buf = self.buf[:end], self.buf[end:]
        return data


def handshake(c):
    head = c.read_until(b"\r\n\r\n").decode("latin1")
    lines = head.split("\r\n")
    parts = lines[0].split(" ")
    if len(parts) != 3 or parts[0] != "GET" or parts[2] != "HTTP/1.1":
        raise ValueError("bad request line: %r" % lines[0])
    key = None
    for line in lines[1:]:
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    if not key:
        raise ValueError("missing Sec-WebSocket-Key")
    accept = base64.b64encode(
        hashlib.sha1((key + GUID).encode()).digest()).decode()
    c.sock.sendall(
        ("HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())


def read_frame(c):
    b0, b1 = c.read(2)
    op = b0 & 0x0F
    fin = bool(b0 & 0x80)
    masked = bool(b1 & 0x80)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack("!H", c.read(2))[0]
    elif n == 127:
        n = struct.unpack("!Q", c.read(8))[0]
    key = c.read(4) if masked else None
    data = c.read(n)
    if key:
        data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
    return op, fin, data


def send_frame(c, op, data, fin=True):
    b0 = (0x80 if fin else 0) | op
    n = len(data)
    if n < 126:
        head = bytes([b0, n])
    elif n < 65536:
        head = bytes([b0, 126]) + struct.pack("!H", n)
    else:
        head = bytes([b0, 127]) + struct.pack("!Q", n)
    c.sock.sendall(head + data)


def handle(c, op, msg):
    if op == 1:
        text = msg.decode("utf-8")
        if text == "frag":
            send_frame(c, 1, b"fragmented-", fin=False)
            send_frame(c, 9, b"p")
            while True:
                hop, _, _ = read_frame(c)
                if hop == 10:
                    break
                if hop == 8:
                    return False
            send_frame(c, 0, b"message", fin=True)
        else:
            send_frame(c, 1, ("echo:" + text).encode())
    elif op == 2:
        send_frame(c, 2, msg)
    return True


def serve(sock):
    c = Conn(sock)
    handshake(c)
    msg_op = 0
    msg = b""
    while True:
        op, fin, data = read_frame(c)
        if op == 8:
            send_frame(c, 8, data)
            return
        if op == 9:
            send_frame(c, 10, data)
            continue
        if op == 10:
            continue
        if op == 0:
            msg += data
        else:
            msg_op, msg = op, data
        if fin:
            if not handle(c, msg_op, msg):
                return
            msg_op, msg = 0, b""


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    conn, _ = srv.accept()
    conn.settimeout(10)
    try:
        serve(conn)
    except EOFError:
        pass  # client went away without a close frame
    except Exception as exc:  # turn any failure into a closed socket
        sys.stderr.write("ws_server: %r\n" % (exc,))
    finally:
        conn.close()
        srv.close()


if __name__ == "__main__":
    main()
