// IPMI 2.0 / RMCP+ client, cipher suite 3 (HMAC-SHA1 auth, HMAC-SHA1-96 integrity,
// AES-CBC-128 confidentiality). Byte-for-byte port of tools/ipmi_proto.py, which was
// validated against a real BMC (Supermicro X9, fw 2.45).
//
// Only what the rescue logic needs: chassis power status and chassis control.
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include "state.h"
#include "net.h"

enum IpmiPower { IPMI_ERR = -1, IPMI_OFF = 0, IPMI_ON = 1 };
enum ChassisAction : uint8_t { CH_OFF = 0x00, CH_ON = 0x01, CH_CYCLE = 0x02, CH_RESET = 0x03, CH_SOFT = 0x05 };

SemaphoreHandle_t gIpmiLock;   // one session at a time (panel, Telegram and monitor all use it)

class IpmiSession {
 public:
  String err;

  IpmiSession(const String& host, const String& user, const String& pass) : host_(host), user_(user) {
    memset(kuid_, 0, sizeof kuid_);
    memcpy(kuid_, pass.c_str(), min<size_t>(pass.length(), 20));
  }
  ~IpmiSession() { if (open_) close(); udp_.stop(); }

  bool open() {
    if (!resolveName(host_, ip_)) return fail("nie rozwiązuję " + host_);
    if (!udp_.begin(0)) return fail("brak gniazda UDP");
    uint8_t buf[128]; size_t n; uint8_t t;

    // Open Session Request (0x10)
    uint8_t req[32] = {0x01, ROLE, 0, 0};
    put32(req + 4, SID_M);
    const uint8_t algos[24] = {0x00, 0, 0, 8, 0x01, 0, 0, 0,    // auth: HMAC-SHA1
                               0x01, 0, 0, 8, 0x01, 0, 0, 0,    // integrity: HMAC-SHA1-96
                               0x02, 0, 0, 8, 0x01, 0, 0, 0};   // confidentiality: AES-CBC-128
    memcpy(req + 8, algos, 24);
    sendRaw(0x10, req, 32, 0, 0, false, false);
    if (!recvRaw(t, buf, n) || t != 0x11 || n < 12) return fail("brak odpowiedzi na Open Session");
    if (buf[1]) return fail("Open Session odrzucone, kod 0x" + String(buf[1], HEX));
    sidC_ = get32(buf + 8);

    // RAKP 1 (0x12)
    uint8_t rm[16]; esp_fill_random(rm, 16);
    const uint8_t role = ROLE | 0x10;           // name-only lookup
    uint8_t ulen = user_.length();
    uint8_t r1[28 + 16];
    memset(r1, 0, sizeof r1);
    r1[0] = 0x02; put32(r1 + 4, sidC_); memcpy(r1 + 8, rm, 16);
    r1[24] = role; r1[27] = ulen; memcpy(r1 + 28, user_.c_str(), ulen);
    sendRaw(0x12, r1, 28 + ulen, 0, 0, false, false);
    if (!recvRaw(t, buf, n) || t != 0x13 || n < 2) return fail("brak RAKP2");
    if (buf[1] == 0x0D) return fail("BMC nie zna konta " + user_);
    if (buf[1]) return fail("RAKP2 kod 0x" + String(buf[1], HEX));
    if (n < 60) return fail("RAKP2 za krótkie");
    uint8_t rc[16], guid[16];
    memcpy(rc, buf + 8, 16); memcpy(guid, buf + 24, 16);

    // RAKP2 auth code proves the BMC knows our password (and we typed the right one).
    uint8_t m[128]; size_t ml = 0;
    put32(m, SID_M); put32(m + 4, sidC_); ml = 8;
    memcpy(m + ml, rm, 16); ml += 16; memcpy(m + ml, rc, 16); ml += 16; memcpy(m + ml, guid, 16); ml += 16;
    m[ml++] = role; m[ml++] = ulen; memcpy(m + ml, user_.c_str(), ulen); ml += ulen;
    uint8_t code[20]; hmac(kuid_, 20, m, ml, code);
    if (memcmp(code, buf + 40, 20)) return fail("złe hasło IPMI");

    // Session keys
    ml = 0; memcpy(m, rm, 16); memcpy(m + 16, rc, 16); ml = 32;
    m[ml++] = role; m[ml++] = ulen; memcpy(m + ml, user_.c_str(), ulen); ml += ulen;
    uint8_t sik[20]; hmac(kuid_, 20, m, ml, sik);
    uint8_t c1[20], c2[20]; memset(c1, 1, 20); memset(c2, 2, 20);
    hmac(sik, 20, c1, 20, k1_); hmac(sik, 20, c2, 20, k2_);

    // RAKP 3 (0x14)
    ml = 0; memcpy(m, rc, 16); ml = 16; put32(m + ml, SID_M); ml += 4;
    m[ml++] = role; m[ml++] = ulen; memcpy(m + ml, user_.c_str(), ulen); ml += ulen;
    uint8_t r3[28] = {0x03, 0, 0, 0};
    put32(r3 + 4, sidC_); hmac(kuid_, 20, m, ml, r3 + 8);
    sendRaw(0x14, r3, 28, 0, 0, false, false);
    if (!recvRaw(t, buf, n) || t != 0x15 || n < 2) return fail("brak RAKP4");
    if (buf[1]) return fail("RAKP4 kod 0x" + String(buf[1], HEX));
    memcpy(m, rm, 16); put32(m + 16, sidC_); memcpy(m + 20, guid, 16);
    uint8_t icv[20]; hmac(sik, 20, m, 36, icv);
    if (n < 20 || memcmp(icv, buf + 8, 12)) return fail("RAKP4: zły podpis BMC");
    open_ = true;

    uint8_t cc, d[8]; size_t dl;
    uint8_t priv = ROLE;
    if (!cmd(0x06, 0x3B, &priv, 1, cc, d, dl) || cc) return fail("Set Session Privilege: " + (err.length() ? err : "kod 0x" + String(cc, HEX)));
    return true;
  }

  // Sends one IPMI request inside the session; returns completion code + response data.
  bool cmd(uint8_t netfn, uint8_t command, const uint8_t* data, size_t dlen, uint8_t& cc, uint8_t* out, size_t& olen) {
    seq_++; rqSeq_ = (rqSeq_ + 1) & 0x3F;
    uint8_t msg[48]; size_t ml = 0;
    msg[ml++] = 0x20; msg[ml++] = netfn << 2; msg[ml] = csum(msg, 2); ml++;
    size_t t0 = ml;
    msg[ml++] = 0x81; msg[ml++] = rqSeq_ << 2; msg[ml++] = command;
    memcpy(msg + ml, data, dlen); ml += dlen;
    msg[ml] = csum(msg + t0, ml - t0); ml++;
    // confidentiality trailer: pad 1,2,3.. + pad length, to a multiple of 16
    size_t pad = (16 - (ml + 1) % 16) % 16;
    for (size_t i = 1; i <= pad; i++) msg[ml++] = i;
    msg[ml++] = pad;
    uint8_t payload[16 + 48];
    esp_fill_random(payload, 16);
    uint8_t iv[16]; memcpy(iv, payload, 16);
    mbedtls_aes_context aes; mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, k2_, 128);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, ml, iv, msg, payload + 16);
    mbedtls_aes_free(&aes);
    sendRaw(0x00, payload, 16 + ml, sidC_, seq_, true, true);

    uint8_t buf[128]; size_t n; uint8_t t;
    if (!recvRaw(t, buf, n) || t != 0x00) return fail("brak odpowiedzi na komendę");
    if (n < 7) return fail("za krótka odpowiedź");
    cc = buf[6];
    olen = n - 8;                      // rqAddr netFn chk rsAddr seq cmd cc ... chk
    if (out && olen) memcpy(out, buf + 7, olen);
    return true;
  }

  void close() {
    uint8_t id[4]; put32(id, sidC_);
    uint8_t cc, d[4]; size_t dl;
    cmd(0x06, 0x3C, id, 4, cc, d, dl);
    open_ = false;
  }

 private:
  static constexpr uint8_t ROLE = 0x03;          // OPERATOR
  static constexpr uint32_t SID_M = 0xA0A2A3A4;  // our session id (any non-zero value)
  String host_, user_;
  IPAddress ip_;
  WiFiUDP udp_;
  uint8_t kuid_[20], k1_[20], k2_[20];
  uint32_t sidC_ = 0, seq_ = 0;
  uint8_t rqSeq_ = 0;
  bool open_ = false;

  bool fail(const String& e) { err = e; return false; }
  static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
  static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
  static uint8_t csum(const uint8_t* b, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; i++) s += b[i]; return -s; }

  static void hmac(const uint8_t* key, size_t klen, const uint8_t* data, size_t dlen, uint8_t out[20]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, klen, data, dlen, out);
  }

  void sendRaw(uint8_t type, const uint8_t* payload, size_t plen, uint32_t sid, uint32_t seq, bool auth, bool enc) {
    uint8_t pkt[160]; size_t n = 0;
    pkt[n++] = 0x06; pkt[n++] = 0x00; pkt[n++] = 0xFF; pkt[n++] = 0x07;      // RMCP
    size_t s = n;                                                          // session header starts here
    pkt[n++] = 0x06;
    pkt[n++] = type | (enc ? 0x80 : 0) | (auth ? 0x40 : 0);
    put32(pkt + n, sid); n += 4; put32(pkt + n, seq); n += 4;
    pkt[n++] = plen & 0xFF; pkt[n++] = plen >> 8;
    memcpy(pkt + n, payload, plen); n += plen;
    if (auth) {
      size_t pad = (4 - (n - s + 2) % 4) % 4;
      for (size_t i = 0; i < pad; i++) pkt[n++] = 0xFF;
      pkt[n++] = pad; pkt[n++] = 0x07;
      uint8_t code[20]; hmac(k1_, 20, pkt + s, n - s, code);
      memcpy(pkt + n, code, 12); n += 12;
    }
    udp_.beginPacket(ip_, 623); udp_.write(pkt, n); udp_.endPacket();
  }

  bool recvRaw(uint8_t& type, uint8_t* out, size_t& olen) {
    uint32_t t0 = millis();
    while (millis() - t0 < 3000) {
      int len = udp_.parsePacket();
      if (len <= 0) { delay(5); continue; }
      uint8_t pkt[256];
      if (len > (int)sizeof pkt) { udp_.flush(); continue; }
      udp_.read(pkt, len);
      if (len < 16 || pkt[0] != 0x06 || pkt[3] != 0x07 || pkt[4] != 0x06) continue;
      uint8_t* b = pkt + 4; size_t bl = len - 4;
      uint8_t pt = b[1];
      size_t plen = b[10] | b[11] << 8;
      if (12 + plen > bl) return fail("uszkodzony pakiet");
      uint8_t* payload = b + 12;
      if (pt & 0x40) {                               // verify HMAC-SHA1-96
        if (bl < 12) return fail("brak podpisu");
        uint8_t code[20]; hmac(k1_, 20, b, bl - 12, code);
        if (memcmp(code, b + bl - 12, 12)) return fail("zły podpis odpowiedzi BMC");
      }
      if (pt & 0x80) {                               // decrypt
        if (plen < 32 || (plen - 16) % 16) return fail("zła długość szyfrogramu");
        uint8_t iv[16]; memcpy(iv, payload, 16);
        size_t cl = plen - 16;
        mbedtls_aes_context aes; mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_dec(&aes, k2_, 128);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cl, iv, payload + 16, out);
        mbedtls_aes_free(&aes);
        uint8_t padl = out[cl - 1];
        if (padl + 1 > cl) return fail("zły padding");
        olen = cl - 1 - padl;
      } else {
        memcpy(out, payload, plen); olen = plen;
      }
      type = pt & 0x3F;
      return true;
    }
    return fail("BMC nie odpowiada (UDP 623)");
  }
};

// ---------- high-level helpers (thread-safe) ----------

static IpmiSession* ipmiOpen(String& err) {
  String host, user, pass;
  { Lock l; host = gSet.ipmiHost; user = gSet.ipmiUser; pass = gSet.ipmiPass; }
  if (host.isEmpty() || user.isEmpty()) { err = "IPMI nieskonfigurowane"; return nullptr; }
  auto* s = new IpmiSession(host, user, pass);
  if (!s->open()) { err = s->err; delete s; return nullptr; }
  return s;
}

IpmiPower ipmiPowerStatus(String& err) {
  xSemaphoreTake(gIpmiLock, portMAX_DELAY);
  IpmiPower res = IPMI_ERR;
  if (IpmiSession* s = ipmiOpen(err)) {
    uint8_t cc, d[8]; size_t dl = 0;
    if (s->cmd(0x00, 0x01, nullptr, 0, cc, d, dl) && cc == 0 && dl >= 1) res = (d[0] & 1) ? IPMI_ON : IPMI_OFF;
    else err = s->err.length() ? s->err : "Get Chassis Status kod 0x" + String(cc, HEX);
    delete s;
  }
  xSemaphoreGive(gIpmiLock);
  return res;
}

bool ipmiChassis(ChassisAction a, String& err) {
  xSemaphoreTake(gIpmiLock, portMAX_DELAY);
  bool ok = false;
  if (IpmiSession* s = ipmiOpen(err)) {
    uint8_t cc, d[8], arg = a; size_t dl = 0;
    ok = s->cmd(0x00, 0x02, &arg, 1, cc, d, dl) && cc == 0;
    if (!ok) err = s->err.length() ? s->err : "Chassis Control kod 0x" + String(cc, HEX);
    delete s;
  }
  xSemaphoreGive(gIpmiLock);
  return ok;
}
