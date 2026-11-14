#!/usr/bin/env python3
"""
TLS 1.2 Test Server — RSA-AES128-GCM-SHA256
Low-level TLS server for debugging client handshake.
Dumps all handshake messages and key material.
"""
import socket, ssl, struct, sys, os, threading, hashlib, hmac
from binascii import hexlify

CERT_FILE = os.path.join(os.path.dirname(__file__), "server.crt")
KEY_FILE  = os.path.join(os.path.dirname(__file__), "server.key")

LOG = open("/tmp/tls_py_server.log", "w", buffering=1)

def log(msg, *args):
    txt = msg % args if args else msg
    print(txt, file=LOG, flush=True)
    print(txt, flush=True)

# ---- Known-answer verification helpers ----
def hexdump(b, label=""):
    if label:
        log(f"  {label}: {hexlify(b).decode()}")
    else:
        log(f"  data: {hexlify(b).decode()}")

class TLS12Server:
    def __init__(self):
        self.client_random = None
        self.server_random = os.urandom(32)
        self.session_id  = os.urandom(32)
        self.pre_master  = bytes([3, 3]) + os.urandom(46)
        self.master_sec  = None
        self.client_write_key  = None
        self.server_write_key  = None
        self.client_write_iv   = None
        self.server_write_iv   = None
        self.client_seq = 0
        self.server_seq = 0
        self.transcript = b""      # concatenated handshake messages (4-byte header+body)
        self.ctx = None
        self.conn = None

    # ---------- TLS 1.2 PRF (P_SHA256) ----------
    @staticmethod
    def p_sha256(secret, seed, length):
        result = b""
        a = seed
        while len(result) < length:
            a = hmac.new(secret, a, hashlib.sha256).digest()
            result += hmac.new(secret, a + seed, hashlib.sha256).digest()
        return result[:length]

    @staticmethod
    def prf_sha256(secret, label, seed, length):
        return TLS12Server.p_sha256(secret, label + seed, length)

    # ---------- Build nonce & AAD ----------
    @staticmethod
    def build_nonce(write_iv, seq_num):
        """TLS 1.2 nonce: write_iv (4) || seq_num (8 BE) = 12 bytes"""
        return write_iv + struct.pack(">Q", seq_num)

    @staticmethod
    def build_aad(seq_num, content_type, version, pt_len):
        """AAD: seq_num(8) || ct(1) || version(2) || pt_len(2) = 13 bytes"""
        return struct.pack(">Q", seq_num) + struct.pack(">B", content_type) + struct.pack(">H", version) + struct.pack(">H", pt_len)

    # ---------- Record layer ----------
    def send_record(self, ct, data):
        header = struct.pack(">BHH", ct, 0x0303, len(data))
        payload = header + data
        self.conn.sendall(payload)
        log(f"  >> Record ct={ct} len={len(data)}")

    def recv_record(self):
        header = self.recv_exact(5)
        ct, ver, length = struct.unpack(">BHH", header)
        body = self.recv_exact(length)
        log(f"  << Record ct={ct} ver={ver:#x} len={length}")
        return ct, body

    def recv_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.conn.recv(n - len(data))
            if not chunk:
                raise ConnectionError("peer closed")
            data += chunk
        return data

    # ---------- AES-128-GCM ----------
    def gcm_encrypt(self, pt, key, iv, aad):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        aesgcm = AESGCM(key)
        ct_with_tag = aesgcm.encrypt(iv, pt, aad)
        return ct_with_tag[:-16], ct_with_tag[-16:]

    def gcm_decrypt(self, ct, key, iv, aad, tag):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        aesgcm = AESGCM(key)
        try:
            pt = aesgcm.decrypt(iv, ct + tag, aad)
            return pt, True
        except Exception as e:
            log(f"  GCM decrypt FAILED: {e}")
            return None, False

    # ---------- Handshake ----------
    def handle_handshake(self):
        # ---- Step 1: Read ClientHello ----
        ct, body = self.recv_record()
        assert ct == 22, f"Expected handshake(22), got {ct}"
        hs_type = body[0]
        assert hs_type == 1, f"Expected ClientHello(1), got {hs_type}"

        hs_len = struct.unpack(">I", b'\x00' + body[1:4])[0]
        hs_body = body[4:4+hs_len]

        # Parse ClientHello
        ver = struct.unpack(">H", hs_body[0:2])[0]
        self.client_random = hs_body[2:34]
        sid_len = hs_body[34]
        sid = hs_body[35:35+sid_len]
        cipher_suites_len = struct.unpack(">H", hs_body[35+sid_len:37+sid_len])[0]
        cipher_suites = hs_body[37+sid_len:37+sid_len+cipher_suites_len]

        log(f"\n=== ClientHello ===")
        log(f"  version: {ver:#x}")
        log(f"  client_random: {hexlify(self.client_random).decode()}")
        log(f"  cipher_suites: {hexlify(cipher_suites).decode()}")

        # Record in transcript: full handshake message
        self.transcript += body[:4+hs_len]  # type(1)+length(3)+body

        # ---- Step 2: Send ServerHello ----
        sh_body = struct.pack(">H", 0x0303) + self.server_random + struct.pack("B", 32) + self.session_id
        sh_body += struct.pack(">H", 0x009C)  # TLS_RSA_WITH_AES_128_GCM_SHA256
        sh_body += struct.pack("B", 1) + b'\x00'  # compression

        # No extensions
        sh_msg = bytes([2]) + struct.pack(">I", len(sh_body))[1:4] + sh_body
        log(f"\n=== ServerHello ===")
        log(f"  server_random: {hexlify(self.server_random).decode()}")

        self.send_record(22, sh_msg)
        self.transcript += sh_msg

        # ---- Step 3: Send Certificate ----
        with open(CERT_FILE, "rb") as f:
            cert_pem = f.read()
        # Load cert via ssl to get DER
        import tempfile
        tf = tempfile.NamedTemporaryFile(delete=False, suffix=".pem")
        tf.write(cert_pem)
        tf.close()
        # Use openssl to convert PEM -> DER
        import subprocess
        der = subprocess.check_output(["openssl", "x509", "-in", tf.name, "-outform", "DER"])
        os.unlink(tf.name)

        cert_len = len(der)
        # Certificate structure: list_len(3) + cert_len(3) + der
        cert_list_body = struct.pack(">I", cert_len)[1:4] + struct.pack(">I", cert_len)[1:4] + der
        cert_msg = bytes([11]) + struct.pack(">I", len(cert_list_body))[1:4] + cert_list_body

        log(f"\n=== Certificate ===")
        log(f"  cert_len={cert_len}")

        self.send_record(22, cert_msg)
        self.transcript += cert_msg

        # ---- Step 4: Send ServerHelloDone ----
        shd_msg = bytes([14]) + b'\x00\x00\x00'
        log(f"\n=== ServerHelloDone ===")
        self.send_record(22, shd_msg)
        self.transcript += shd_msg

        # ---- Step 5: Read ClientKeyExchange ----
        ct2, body2 = self.recv_record()
        log(f"\n=== ClientKeyExchange ===")
        assert ct2 == 22, f"Expected handshake(22), got {ct2}"
        hs_type2 = body2[0]
        assert hs_type2 == 16, f"Expected CKE(16), got {hs_type2}"

        cke_body = body2[4:]
        enc_pms_len = struct.unpack(">H", cke_body[0:2])[0]
        enc_pms = cke_body[2:2+enc_pms_len]

        log(f"  encrypted_pms_len={enc_pms_len}")

        # Decrypt PMS with server key
        from cryptography.hazmat.primitives import serialization, hashes
        from cryptography.hazmat.primitives.asymmetric import padding

        with open(KEY_FILE, "rb") as f:
            privkey = serialization.load_pem_private_key(f.read(), password=None)

        self.pre_master = privkey.decrypt(
            enc_pms,
            padding.PKCS1v15()
        )
        log(f"  pre_master_secret: {hexlify(self.pre_master).decode()}")

        self.transcript += body2[:4+len(cke_body)]

        # ---- Derive keys ----
        # master_secret = PRF(pre_master, "master secret", CR || SR)
        ms_seed = b"master secret" + self.client_random + self.server_random
        self.master_sec = self.prf_sha256(self.pre_master, b"master secret",
                                          self.client_random + self.server_random, 48)

        log(f"\n=== Key Derivation ===")
        log(f"  master_secret: {hexlify(self.master_sec).decode()}")

        # key_block = PRF(master, "key expansion", SR || CR)
        kb_seed = b"key expansion" + self.server_random + self.client_random
        key_block = self.prf_sha256(self.master_sec, b"key expansion",
                                    self.server_random + self.client_random, 40)

        self.client_write_key = key_block[0:16]
        self.server_write_key = key_block[16:32]
        self.client_write_iv  = key_block[32:36]
        self.server_write_iv  = key_block[36:40]

        log(f"  client_write_key: {hexlify(self.client_write_key).decode()}")
        log(f"  server_write_key: {hexlify(self.server_write_key).decode()}")
        log(f"  client_write_iv:  {hexlify(self.client_write_iv).decode()}")
        log(f"  server_write_iv:  {hexlify(self.server_write_iv).decode()}")

        # ---- Step 6: Read CCS ----
        ct3, body3 = self.recv_record()
        log(f"\n=== CCS ===")
        log(f"  ct={ct3} body={hexlify(body3).decode()}")
        assert ct3 == 20, f"Expected CCS(20), got {ct3}"

        # ---- Step 7: Read Finished (encrypted) ----
        ct4, body4 = self.recv_record()
        log(f"\n=== Client Finished (encrypted) ===")
        log(f"  ct={ct4} len={len(body4)}")
        log(f"  raw: {hexlify(body4).decode()}")

        # Parse encrypted record
        explicit_nonce = body4[:8]
        ct_ciphertext = body4[8:-16]
        tag = body4[-16:]

        log(f"  explicit_nonce: {hexlify(explicit_nonce).decode()}")
        log(f"  ct_len={len(ct_ciphertext)} tag={hexlify(tag).decode()}")

        # Build nonce: client_write_iv || explicit_nonce
        nonce = self.client_write_iv + explicit_nonce
        log(f"  nonce (iv4+exp8): {hexlify(nonce).decode()}")

        # ---- Try BOTH AAD content types ----
        for aad_ct in [23, 22]:
            aad = self.build_aad(self.client_seq, aad_ct, 0x0303, len(ct_ciphertext))
            log(f"  Trying AAD ct={aad_ct}: {hexlify(aad).decode()}")
            pt, ok = self.gcm_decrypt(ct_ciphertext, self.client_write_key, nonce, aad, tag)
            if ok:
                log(f"  *** DECRYPT SUCCESS with AAD ct={aad_ct} ***")
                log(f"  plaintext: {hexlify(pt).decode()}")
                if pt:
                    hs_type_fin = pt[0]
                    fin_body_len = struct.unpack(">I", b'\x00' + pt[1:4])[0]
                    verify_data = pt[4:4+fin_body_len]
                    log(f"  finished_type={hs_type_fin} body_len={fin_body_len}")
                    log(f"  verify_data: {hexlify(verify_data).decode()}")

                    # Verify verify_data
                    # transcript_hash = SHA256(transcript)
                    thash = hashlib.sha256(self.transcript).digest()
                    log(f"  transcript_hash: {hexlify(thash).decode()}")
                    expected_vd = self.prf_sha256(self.master_sec, b"client finished", thash, 12)
                    log(f"  expected_vd: {hexlify(expected_vd).decode()}")
                    if verify_data == expected_vd:
                        log(f"  *** VERIFY_DATA MATCH ***")
                    else:
                        log(f"  *** VERIFY_DATA MISMATCH ***")
                break
            else:
                log(f"  Decrypt FAILED with ct={aad_ct}")

        if not pt or not ok:
            log("  FATAL: Could not decrypt Finished with any AAD ct")
            self.send_alert(2, 40)
            return

        self.client_seq += 1

        # ---- Step 8: Send CCS ----
        log(f"\n=== Send CCS ===")
        self.send_record(20, b'\x01')

        # ---- Step 9: Send Server Finished (encrypted) ----
        log(f"\n=== Send Server Finished ===")
        # compute verify_data
        thash = hashlib.sha256(self.transcript).digest()
        log(f"  transcript_hash: {hexlify(thash).decode()}")
        srv_vd = self.prf_sha256(self.master_sec, b"server finished", thash, 12)
        log(f"  server_verify_data: {hexlify(srv_vd).decode()}")

        srv_fin = bytes([20]) + struct.pack(">I", 12)[1:4] + srv_vd
        log(f"  server_finished_msg: {hexlify(srv_fin).decode()}")

        # Encrypt
        nonce_srv = self.build_nonce(self.server_write_iv, self.server_seq)
        aad_srv = self.build_aad(self.server_seq, 22, 0x0303, len(srv_fin))
        log(f"  encrypt nonce: {hexlify(nonce_srv).decode()}")
        log(f"  encrypt aad: {hexlify(aad_srv).decode()}")

        ct_srv, tag_srv = self.gcm_encrypt(srv_fin, self.server_write_key, nonce_srv, aad_srv)
        log(f"  ciphertext: {hexlify(ct_srv).decode()} tag: {hexlify(tag_srv).decode()}")

        explicit_nonce_srv = struct.pack(">Q", self.server_seq)
        enc_record = explicit_nonce_srv + ct_srv + tag_srv
        log(f"  sending ct=22, enc_record_len={len(enc_record)}")
        self.send_record(22, enc_record)
        self.server_seq += 1

        # ---- Step 10: Read Application Data ----
        log(f"\n=== Read Application Data (encrypted) ===")
        ct5, body5 = self.recv_record()
        log(f"  ct={ct5} len={len(body5)} raw: {hexlify(body5).decode()}")

        if ct5 == 23:
            exp_nonce = body5[:8]
            ct_adata = body5[8:-16]
            tag5 = body5[-16:]

            nonce5 = self.client_write_iv + exp_nonce
            aad5 = self.build_aad(self.client_seq, 23, 0x0303, len(ct_adata))

            pt5, ok5 = self.gcm_decrypt(ct_adata, self.client_write_key, nonce5, aad5, tag5)
            if ok5:
                log(f"  DECRYPTED: {pt5.decode(errors='replace')!r}")
                # Send response
                resp = b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello"
                resp_nonce = self.build_nonce(self.server_write_iv, self.server_seq)
                resp_aad = self.build_aad(self.server_seq, 23, 0x0303, len(resp))
                resp_ct, resp_tag = self.gcm_encrypt(resp, self.server_write_key, resp_nonce, resp_aad)
                resp_enc = struct.pack(">Q", self.server_seq) + resp_ct + resp_tag
                self.send_record(23, resp_enc)
                self.server_seq += 1
                log("  Response sent!")
            else:
                log("  Could not decrypt app data")

        # ---- Step 11: Read Close ----
        try:
            ct6, body6 = self.recv_record()
            log(f"\n=== Close ct={ct6} body={hexlify(body6).decode()}")
        except:
            pass

        log(f"\n=== HAND SHAKE COMPLETE ===")

    def send_alert(self, level, desc):
        self.send_record(21, bytes([level, desc]))

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", 4433))
        sock.listen(5)
        log(f"Python TLS 1.2 server on 127.0.0.1:4433")

        while True:
            conn, addr = sock.accept()
            log(f"\n{'='*60}")
            log(f"Connection from {addr}")
            self.conn = conn
            try:
                self.handle_handshake()
            except Exception as e:
                log(f"Error: {e}")
                import traceback
                traceback.print_exc(file=LOG)
                traceback.print_exc()
            finally:
                conn.close()
                log("Connection closed")

if __name__ == "__main__":
    svr = TLS12Server()
    svr.run()
