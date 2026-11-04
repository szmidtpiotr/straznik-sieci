// Thread-safe name resolution.
//
// WiFi.hostByName() in core 2.0.17 calls lwIP's raw dns_gethostbyname() straight from the
// calling task (the raw API may only run in the tcpip thread) and leaves a callback pointing
// at its stack frame when it gives up after 15 s. With several tasks resolving names this
// corrupted lwIP's DNS table: panic in dns_send -> udp_sendto (core dump, 27.09 11:57).
// getaddrinfo() goes through the netconn API and runs inside the tcpip thread.
// Rule: never call WiFi.hostByName(), and never hand a host NAME to WiFiClient/
// WiFiClientSecure::connect() — resolve here, connect by IP. And no HTTPClient: when the
// server closes the connection early, HTTPClient silently reconnects BY NAME (the unsafe
// path again). httpExchange() below speaks HTTP over a connection we opened ourselves.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "lwip/netdb.h"
#include "esp_netif.h"

bool resolveName(const String& host, IPAddress& out) {
  if (out.fromString(host)) return true;
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  out = IPAddress(((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
  freeaddrinfo(res);
  return (uint32_t)out != 0;
}

// Public resolvers for the monitor itself (the local DNS is usually a probed target). Goes through
// esp_netif, which applies the change inside the tcpip thread.
void setPublicDns(IPAddress primary, IPAddress secondary) {
  esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!nif) return;
  esp_netif_dns_info_t d = {};
  d.ip.type = ESP_IPADDR_TYPE_V4;
  d.ip.u_addr.ip4.addr = (uint32_t)primary;
  esp_netif_set_dns_info(nif, ESP_NETIF_DNS_MAIN, &d);
  d.ip.u_addr.ip4.addr = (uint32_t)secondary;
  esp_netif_set_dns_info(nif, ESP_NETIF_DNS_BACKUP, &d);
}

// Minimal HTTP/1.1 exchange over an already-connected client. Never reconnects.
// Returns the status code (>0) or a negative error with `err` set. `body` of the response is
// read only when respBody != nullptr (capped at 32 KB, Content-Length or chunked).
int httpExchange(Client& c, const char* method, const String& host, const String& path,
                 const String& contentType, const String& body, String* respBody,
                 uint32_t timeoutMs, String& err) {
  String req = String(method) + " " + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nUser-Agent: straznik\r\nAccept: */*\r\nConnection: close\r\n";
  if (body.length() || !strcmp(method, "POST"))
    req += "Content-Type: " + contentType + "\r\nContent-Length: " + String(body.length()) + "\r\n";
  req += "\r\n";
  if (c.write((const uint8_t*)req.c_str(), req.length()) != req.length() ||
      (body.length() && c.write((const uint8_t*)body.c_str(), body.length()) != body.length())) {
    err = "nie udało się wysłać zapytania"; return -1;
  }
  uint32_t t0 = millis();
  auto readLine = [&](String& out) -> bool {
    out = "";
    while (millis() - t0 < timeoutMs) {
      while (c.available()) {
        char ch = c.read();
        if (ch == '\n') { out.trim(); return true; }
        if (out.length() < 1024) out += ch;
      }
      if (!c.connected()) return out.length() > 0;
      delay(2);
    }
    return false;
  };
  String line;
  if (!readLine(line) || !line.startsWith("HTTP/")) { err = line.length() ? "zła odpowiedź: " + line.substring(0, 40) : "brak odpowiedzi"; return -2; }
  int code = line.substring(line.indexOf(' ') + 1).toInt();
  if (code <= 0) { err = "zła odpowiedź: " + line.substring(0, 40); return -3; }
  if (!respBody) return code;
  long clen = -1; bool chunked = false; String h;
  while (readLine(h) && h.length()) {
    String l = h; l.toLowerCase();
    if (l.startsWith("content-length:")) clen = l.substring(15).toInt();
    if (l.startsWith("transfer-encoding:") && l.indexOf("chunked") >= 0) chunked = true;
  }
  const size_t CAP = 32768;
  auto readN = [&](long n) {
    while (n > 0 && millis() - t0 < timeoutMs) {
      if (c.available()) { char ch = c.read(); if (respBody->length() < CAP) *respBody += ch; n--; }
      else if (!c.connected()) break; else delay(2);
    }
  };
  if (chunked) {
    String sz;
    while (readLine(sz)) {
      long n = strtol(sz.c_str(), nullptr, 16);
      if (n <= 0) break;
      readN(n);
      String crlf; readLine(crlf);
    }
  } else if (clen >= 0) readN(clen);
  else readN(CAP);   // no length: read until the server closes
  return code;
}
