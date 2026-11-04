// Probes: ICMP ping, TCP connect, HTTP status, DNS query. Each returns true when
// the target answered and fills `ms` (response time) or `err`.
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include "ping/ping_sock.h"
#include "state.h"
#include "net.h"

static bool resolveHost(const String& host, IPAddress& ip) { return resolveName(host, ip); }

// ---------- ping ----------

struct PingCtx { SemaphoreHandle_t done; bool ok; uint32_t ms; };

static void onPingSuccess(esp_ping_handle_t h, void* arg) {
  auto* c = (PingCtx*)arg;
  uint32_t t; esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof t);
  c->ok = true; c->ms = t;
}
static void onPingEnd(esp_ping_handle_t h, void* arg) { xSemaphoreGive(((PingCtx*)arg)->done); }

// Up to 3 echo requests; one reply is enough (a single lost packet on Wi-Fi is not an outage).
bool probePing(const IPAddress& ip, uint32_t& ms, String& err) {
  PingCtx ctx{xSemaphoreCreateBinary(), false, 0};
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr.type = IPADDR_TYPE_V4;
  cfg.target_addr.u_addr.ip4.addr = static_cast<uint32_t>(ip);
  cfg.count = 3;
  cfg.interval_ms = 300;
  cfg.timeout_ms = 1000;
  cfg.task_stack_size = 4096;
  esp_ping_callbacks_t cb = {};
  cb.cb_args = &ctx;
  cb.on_ping_success = onPingSuccess;
  cb.on_ping_end = onPingEnd;
  esp_ping_handle_t h;
  if (esp_ping_new_session(&cfg, &cb, &h) != ESP_OK) { vSemaphoreDelete(ctx.done); err = "ping: brak sesji"; return false; }
  esp_ping_start(h);
  bool finished = xSemaphoreTake(ctx.done, pdMS_TO_TICKS(6000)) == pdTRUE;
  esp_ping_stop(h);
  esp_ping_delete_session(h);
  vSemaphoreDelete(ctx.done);
  if (!finished) { err = "ping: przekroczony czas"; return false; }
  if (!ctx.ok) { err = "brak odpowiedzi na ping"; return false; }
  ms = ctx.ms;
  return true;
}

// ---------- tcp ----------

bool probeTcp(const IPAddress& ip, uint16_t port, uint32_t& ms, String& err) {
  WiFiClient c;
  uint32_t t0 = millis();
  if (!c.connect(ip, port, PROBE_TIMEOUT_MS)) { err = "port " + String(port) + " zamknięty / brak odpowiedzi"; return false; }
  ms = millis() - t0;
  c.stop();
  return true;
}

// ---------- http ----------

// Days from now until an X.509 "valid to" date (negative = already expired).
static int16_t daysUntil(const mbedtls_x509_time& v) {
  struct tm tm = {};
  tm.tm_year = v.year - 1900; tm.tm_mon = v.mon - 1; tm.tm_mday = v.day; tm.tm_hour = v.hour;
  time_t exp = mktime(&tm), now = time(nullptr);
  return (int16_t)((exp - now) / 86400);
}

// Extra results some probes can report.
struct ProbeExtra { int16_t certDays = -1; uint32_t fullMs = 0; };

bool probeHttp(const Target& t, const IPAddress& ip, uint32_t& ms, String& err, ProbeExtra& x) {
  int16_t& certDays = x.certDays;
  // Internal HTTPS uses self-signed certs, so only reachability is checked there, not identity.
  bool tls = t.tls;
  // A target given by name keeps the name in the URL: HTTPS needs it for SNI and virtual hosts
  // (NPM rejects the handshake for a bare IP). IP targets use the already-resolved address.
  IPAddress literal;
  String hostPart = literal.fromString(t.host) ? ip.toString() : t.host;
  String hostHeader = hostPart + ((t.port == 80 && !tls) || (t.port == 443 && tls) ? "" : ":" + String(t.port));
  WiFiClient plain;
  WiFiClientSecure secure;
  // For HTTPS the ESP itself spends ~1 s on the TLS key exchange, which says nothing about the
  // server. Measure a bare TCP connect first and report that as the response time.
  uint32_t connectMs = 0;
  if (tls) {
    WiFiClient probe;
    uint32_t c0 = millis();
    if (probe.connect(ip, t.port, PROBE_TIMEOUT_MS)) { connectMs = millis() - c0; probe.stop(); }
    else { err = "port " + String(t.port) + " nie odpowiada"; return false; }
  }
  uint32_t t0 = millis();
  // Connect ourselves by IP (name only for SNI) and speak HTTP directly (httpExchange):
  // nothing here may ever reconnect by name.
  Client* conn;
  if (tls) {
    secure.setInsecure(); secure.setHandshakeTimeout(8); secure.setTimeout(HTTP_TIMEOUT_MS / 1000);
    if (!secure.connect(ip, t.port, t.host.c_str(), nullptr, nullptr, nullptr)) { err = "https: nie udało się nawiązać szyfrowanego połączenia"; return false; }
    conn = &secure;
  } else {
    if (!plain.connect(ip, t.port, PROBE_TIMEOUT_MS)) { err = "port " + String(t.port) + " nie odpowiada"; return false; }
    conn = &plain;
  }
  String herr;
  int code = httpExchange(*conn, "GET", hostHeader, t.path.length() ? t.path : "/", "", "", nullptr, HTTP_TIMEOUT_MS, herr);
  ms = millis() - t0;
  x.fullMs = ms;
  if (tls) ms = max<uint32_t>(connectMs, 1);
  if (tls && code > 0 && timeValid()) {
    const mbedtls_x509_crt* c = secure.getPeerCertificate();
    if (c) certDays = daysUntil(c->valid_to);
  }
  conn->stop();
  if (code <= 0) { err = "http: " + herr; return false; }
  if (t.expect ? code != t.expect : code >= 500) {
    err = code == 502 || code == 504 ? "http: kod " + String(code) + " — NPM działa, usługa za nim nie" : "http: kod " + String(code);
    return false;
  }
  if (certDays >= 0 && certDays < CERT_WARN_DAYS) { err = "certyfikat wygasa za " + String(certDays) + " dni"; return false; }
  return true;
}

// ---------- dns ----------

// Sends one A query straight to the server under test (not through the system resolver).
bool probeDns(const IPAddress& server, const String& name, uint32_t& ms, String& err) {
  uint8_t q[300];
  uint16_t id = esp_random() & 0xFFFF;
  size_t n = 0;
  q[n++] = id >> 8; q[n++] = id & 0xFF;
  q[n++] = 0x01; q[n++] = 0x00;            // standard query, recursion desired
  q[n++] = 0; q[n++] = 1;                  // QDCOUNT
  for (int i = 0; i < 6; i++) q[n++] = 0;  // AN/NS/AR
  int start = 0;
  while (start <= (int)name.length()) {
    int dot = name.indexOf('.', start);
    if (dot < 0) dot = name.length();
    int len = dot - start;
    if (len <= 0 || len > 63 || n + len + 6 > sizeof q) { err = "dns: zła nazwa"; return false; }
    q[n++] = len;
    memcpy(q + n, name.c_str() + start, len); n += len;
    start = dot + 1;
  }
  q[n++] = 0;
  q[n++] = 0; q[n++] = 1;                  // QTYPE A
  q[n++] = 0; q[n++] = 1;                  // QCLASS IN

  WiFiUDP udp;
  if (!udp.begin(0)) { err = "dns: brak gniazda"; return false; }
  uint32_t t0 = millis();
  udp.beginPacket(server, 53); udp.write(q, n); udp.endPacket();
  bool ok = false;
  while (millis() - t0 < PROBE_TIMEOUT_MS) {
    int len = udp.parsePacket();
    if (len >= 12) {
      uint8_t r[12]; udp.read(r, 12);
      if (((r[0] << 8) | r[1]) != id) continue;
      int rcode = r[3] & 0x0F;
      int ancount = (r[6] << 8) | r[7];
      if (rcode != 0) err = "dns: błąd serwera rcode " + String(rcode);
      else if (ancount == 0) err = "dns: pusta odpowiedź";
      else ok = true;
      break;
    }
    delay(10);
  }
  udp.stop();
  ms = millis() - t0;
  if (!ok && err.isEmpty()) err = "dns: brak odpowiedzi";
  return ok;
}

bool runProbe(const Target& t, uint32_t& ms, String& err, ProbeExtra& x) {
  IPAddress ip;
  if (!resolveHost(t.host, ip)) { err = "nie rozwiązuję nazwy " + t.host; return false; }
  switch (t.type) {
    case P_TCP:  return probeTcp(ip, t.port, ms, err);
    case P_HTTP: return probeHttp(t, ip, ms, err, x);
    case P_DNS:  return probeDns(ip, t.query, ms, err);
    default:     return probePing(ip, ms, err);
  }
}
