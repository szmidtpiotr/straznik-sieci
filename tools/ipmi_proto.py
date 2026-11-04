#!/usr/bin/env python3
"""Reference IPMI 2.0 / RMCP+ client (cipher suite 3: HMAC-SHA1, HMAC-SHA1-96, AES-CBC-128).

Written to validate the exact byte layout against a real BMC (Supermicro X9) before porting it
to the ESP32 firmware (firmware/src/ipmi.h mirrors this file step by step).

Usage:  ipmi_proto.py <host> <user> <password-file> status
        ipmi_proto.py <host> <user> <password-file> reset|cycle     (really resets the server!)
"""
import hashlib
import hmac
import os
import socket
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROLE_OPERATOR = 0x03


def h(key, data):
    return hmac.new(key, data, hashlib.sha1).digest()


def checksum(b):
    return (-sum(b)) & 0xFF


class Ipmi:
    def __init__(self, host, user, password):
        self.addr = (host, 623)
        self.user = user.encode()
        self.kuid = password.encode().ljust(20, b"\0")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(3)
        self.sid_m = 0xA0A2A3A4          # our (remote console) session id
        self.sid_c = 0                   # BMC (managed system) session id
        self.seq = 0
        self.rq_seq = 0
        self.k1 = self.k2 = None

    # ---------- framing ----------
    def _send_raw(self, payload_type, payload, session_id=0, seq=0, auth=False, enc=False):
        pt = payload_type | (0x80 if enc else 0) | (0x40 if auth else 0)
        body = struct.pack("<BBIIH", 0x06, pt, session_id, seq, len(payload)) + payload
        if auth:
            pad = (4 - (len(body) + 2) % 4) % 4
            body += b"\xff" * pad + bytes([pad, 0x07])
            body += h(self.k1, body)[:12]
        pkt = bytes([0x06, 0x00, 0xFF, 0x07]) + body
        self.sock.sendto(pkt, self.addr)

    def _recv(self):
        data, _ = self.sock.recvfrom(1024)
        assert data[:4] == bytes([0x06, 0x00, 0xFF, 0x07]), "not RMCP/IPMI"
        body = data[4:]
        assert body[0] == 0x06, "not RMCP+ session"
        pt = body[1]
        plen = struct.unpack_from("<H", body, 10)[0]
        payload = body[12:12 + plen]
        if pt & 0x40:                     # authenticated: verify HMAC-SHA1-96
            covered = body[:-12]
            if h(self.k1, covered)[:12] != body[-12:]:
                raise ValueError("bad integrity code from BMC")
        if pt & 0x80:                     # encrypted
            iv, ct = payload[:16], payload[16:]
            d = Cipher(algorithms.AES(self.k2[:16]), modes.CBC(iv)).decryptor()
            pt_data = d.update(ct) + d.finalize()
            padlen = pt_data[-1]
            payload = pt_data[:-1 - padlen]
        return pt & 0x3F, payload

    # ---------- session setup ----------
    def open(self):
        # Open Session Request (0x10)
        req = bytes([0x01, ROLE_OPERATOR, 0, 0]) + struct.pack("<I", self.sid_m)
        req += bytes([0x00, 0, 0, 8, 0x01, 0, 0, 0])   # auth: HMAC-SHA1
        req += bytes([0x01, 0, 0, 8, 0x01, 0, 0, 0])   # integrity: HMAC-SHA1-96
        req += bytes([0x02, 0, 0, 8, 0x01, 0, 0, 0])   # confidentiality: AES-CBC-128
        self._send_raw(0x10, req)
        t, r = self._recv()
        assert t == 0x11 and r[1] == 0, f"open session status 0x{r[1]:02x}"
        self.sid_c = struct.unpack_from("<I", r, 8)[0]

        # RAKP 1 (0x12)
        rm = os.urandom(16)
        role = ROLE_OPERATOR | 0x10       # name-only lookup
        rakp1 = bytes([0x02, 0, 0, 0]) + struct.pack("<I", self.sid_c) + rm
        rakp1 += bytes([role, 0, 0, len(self.user)]) + self.user
        self._send_raw(0x12, rakp1)
        t, r = self._recv()
        assert t == 0x13, "no RAKP2"
        if r[1] != 0:
            raise ValueError(f"RAKP2 status 0x{r[1]:02x} (0x0d = unknown user, 0x12 = bad password?)")
        rc, guid, code = r[8:24], r[24:40], r[40:60]
        expect = h(self.kuid, struct.pack("<II", self.sid_m, self.sid_c) + rm + rc + guid +
                   bytes([role, len(self.user)]) + self.user)
        if expect != code:
            raise ValueError("RAKP2 auth code mismatch: wrong password")

        sik = h(self.kuid, rm + rc + bytes([role, len(self.user)]) + self.user)
        self.k1 = h(sik, b"\x01" * 20)
        self.k2 = h(sik, b"\x02" * 20)

        # RAKP 3 (0x14)
        code3 = h(self.kuid, rc + struct.pack("<I", self.sid_m) + bytes([role, len(self.user)]) + self.user)
        self._send_raw(0x14, bytes([0x03, 0, 0, 0]) + struct.pack("<I", self.sid_c) + code3)
        t, r = self._recv()
        assert t == 0x15, "no RAKP4"
        if r[1] != 0:
            raise ValueError(f"RAKP4 status 0x{r[1]:02x}")
        icv = h(sik, rm + struct.pack("<I", self.sid_c) + guid)[:12]
        if r[8:20] != icv:
            raise ValueError("RAKP4 integrity check mismatch")

        cc, _ = self.cmd(0x06, 0x3B, bytes([ROLE_OPERATOR]))   # Set Session Privilege Level
        assert cc == 0, f"set privilege cc 0x{cc:02x}"

    # ---------- IPMI commands over the session ----------
    def cmd(self, netfn, command, data=b""):
        self.seq += 1
        self.rq_seq = (self.rq_seq + 1) & 0x3F
        hdr = bytes([0x20, netfn << 2])
        msg = hdr + bytes([checksum(hdr)])
        tail = bytes([0x81, self.rq_seq << 2, command]) + data
        msg += tail + bytes([checksum(tail)])
        # AES-CBC-128 with confidentiality trailer: pad bytes 1,2,3.. then pad length
        padlen = (16 - (len(msg) + 1) % 16) % 16
        plain = msg + bytes(range(1, padlen + 1)) + bytes([padlen])
        iv = os.urandom(16)
        e = Cipher(algorithms.AES(self.k2[:16]), modes.CBC(iv)).encryptor()
        payload = iv + e.update(plain) + e.finalize()
        self._send_raw(0x00, payload, self.sid_c, self.seq, auth=True, enc=True)
        t, r = self._recv()
        assert t == 0x00, f"unexpected payload type {t}"
        # r = rqAddr, netFn/LUN, chk, rsAddr, seq/LUN, cmd, cc, data..., chk
        return r[6], r[7:-1]

    def close(self):
        try:
            self.cmd(0x06, 0x3C, struct.pack("<I", self.sid_c))
        except Exception:
            pass


def main():
    host, user, pwfile, action = sys.argv[1:5]
    pw = open(pwfile).read().strip()
    s = Ipmi(host, user, pw)
    s.open()
    try:
        if action == "status":
            cc, d = s.cmd(0x00, 0x01)                       # Get Chassis Status
            print("cc", cc, "power", "ON" if d[0] & 1 else "OFF", "raw", d.hex())
        elif action in ("reset", "cycle"):
            cc, _ = s.cmd(0x00, 0x02, bytes([0x03 if action == "reset" else 0x02]))
            print("chassis control cc", cc)
    finally:
        s.close()


if __name__ == "__main__":
    main()
