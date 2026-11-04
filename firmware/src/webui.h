// Web panel: tabs Posterunki / Serwer / Dziennik / Ustawienia (page in ui_html.h),
// JSON API, OTA (/update). Everything behind HTTP Basic auth (admin / PANEL_PASS). LAN only.
#pragma once
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include "state.h"
#include "monitor.h"
#include "telegram.h"
#include "mqtt.h"
#include "rescue.h"
#include "ui_html.h"

WebServer server(80);
HTTPUpdateServer updater;

static bool auth() {
  if (server.authenticate("admin", PANEL_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, "Straznik");
  return false;
}

static const char* stateName(TargetState s) { return s == T_UP ? "up" : s == T_DOWN ? "down" : "unknown"; }

static void handleStatus() {
  if (!auth()) return;
  JsonDocument d;
  {
    Lock l;
    int up = 0, down = 0, total = 0;
    JsonArray ts = d["targets"].to<JsonArray>();
    for (auto& t : gTargets) {
      JsonObject o = ts.add<JsonObject>();
      o["id"] = t.id; o["name"] = t.name; o["type"] = typeName(t.type); o["host"] = t.host;
      if (t.port) o["port"] = t.port;
      o["path"] = t.path; o["query"] = t.query; o["parent"] = t.parent;
      o["enabled"] = t.enabled; o["state"] = stateName(t.state); o["fails"] = t.fails;
      o["ms"] = t.lastMs; o["err"] = t.lastErr; o["since"] = (long)t.since;
      o["suppressed"] = t.state == T_DOWN && parentFailing(t);
      o["muted_until"] = targetMuted(t) ? (long long)t.mutedUntil : 0;
      if (t.every > 1) o["every"] = t.every;
      if (t.needs.length()) o["needs"] = t.needs;
      if (t.certDays >= 0) o["cert_days"] = t.certDays;
      if (t.tls && t.fullMs) o["full_ms"] = t.fullMs;
      if (!monitored(t)) continue;
      total++; up += t.state == T_UP; down += t.state == T_DOWN;
    }
    d["up"] = up; d["down"] = down; d["total"] = total;
    d["muted_until"] = isMuted() ? (long)gMutedUntil : 0;
    d["sweeps"] = gSweeps; d["last_sweep"] = (long)gLastSweep;
    d["interval"] = gSet.interval; d["fail_limit"] = gSet.failLimit; d["heartbeat_hour"] = gSet.heartbeatHour; d["remind_min"] = gSet.remindMin;
    JsonObject rs = d["rescue"].to<JsonObject>();
    rs["target"] = gSet.rescueTarget; rs["hung_min"] = gSet.rescueHungMin; rs["auto"] = gSet.rescueAuto;
    rs["auto_min"] = gSet.rescueAutoMin;
    rs["win_start"] = hhmm(gSet.rescueWinStart); rs["win_end"] = hhmm(gSet.rescueWinEnd); rs["in_window"] = rescueInWindow(); rs["ipmi_host"] = gSet.ipmiHost; rs["ipmi_user"] = gSet.ipmiUser;
    rs["state"] = rescueStateName(gRescue.st);
    rs["dry_run"] = (bool)gRescueDryRun; rs["simulating"] = gSimStart != 0;
    rs["power_checked"] = gRescue.lastPowerCheck > 0;
    rs["power"] = gRescue.power == IPMI_ON ? "włączone" : gRescue.power == IPMI_OFF ? "WYŁĄCZONE" : ("błąd: " + gRescue.powerErr);
    JsonObject mq = d["mqtt"].to<JsonObject>();
    mq["host"] = gSet.mqttHost; mq["port"] = gSet.mqttPort; mq["user"] = gSet.mqttUser; mq["status"] = gMqttStatus;
    d["ssid_cfg"] = gSet.wifiSsid;
    JsonObject tg = d["tg"].to<JsonObject>();
    tg["configured"] = gSet.tgToken.length() && gSet.tgChat.length();
    tg["token_set"] = gSet.tgToken.length() > 0;
    tg["chat"] = gSet.tgChat; tg["chat_set"] = gSet.tgChat.length() > 0;
    tg["stage"] = (const char*)gTgStage; tg["loop_age_ms"] = millis() - gTgLoopMs;
    tg["queue"] = gTgQueue.size(); tg["error"] = gTgLastError; tg["last_ok"] = (long)gTgLastOk;
    tg["pending_chat"] = gTgPendingChat; tg["pending_name"] = gTgPendingName;
    d["probing"] = gProbing;
  }
  d["fw"] = FW_VERSION; d["uptime"] = millis() / 1000; d["now"] = (long)time(nullptr);
  d["ip"] = WiFi.localIP().toString(); d["ssid"] = WiFi.SSID(); d["rssi"] = WiFi.RSSI();
  d["mac"] = WiFi.macAddress(); d["heap"] = ESP.getFreeHeap(); d["psram"] = ESP.getFreePsram();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleLog() {
  if (!auth()) return;
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  { Lock l; for (auto it = gLog.rbegin(); it != gLog.rend(); ++it) { JsonObject e = a.add<JsonObject>(); e["t"] = (long)it->t; e["m"] = it->msg; } }
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleTgLog() {
  if (!auth()) return;
  static const char* names[] = {"sent", "retry", "dropped", "in"};
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  {
    Lock l;
    for (auto it = gTgLog.rbegin(); it != gTgLog.rend(); ++it) {
      JsonObject e = a.add<JsonObject>();
      e["t"] = (long)it->t; e["st"] = names[it->st]; e["text"] = it->text;
      if (it->err.length()) e["err"] = it->err;
    }
  }
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

// One target with its response-time history (oldest first, null = no answer).
static void handleTarget() {
  if (!auth()) return;
  JsonDocument d;
  {
    Lock l;
    Target* t = findTarget(server.arg("id"));
    if (!t) { server.send(404, "text/plain", "nie ma celu"); return; }
    d["id"] = t->id; d["name"] = t->name;
    JsonArray h = d["hist"].to<JsonArray>();
    for (uint8_t i = 0; i < t->histLen; i++) {
      uint16_t v = t->hist[(t->histPos + 60 - t->histLen + i) % 60];
      if (v == 0xFFFF) h.add(nullptr); else h.add(v);
    }
  }
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleAction() {
  if (!auth()) return;
  String a = server.arg("do");
  if (a == "sweep") { runSweepNow(); logEvent("Ręczne sprawdzenie z panelu"); }
  else if ((a == "mute" || a == "unmute") && server.hasArg("id")) {
    long min = a == "mute" ? server.arg("min").toInt() : 0;
    if (a == "mute" && min == 0) min = 60;
    String id = server.arg("id");
    if (!setTargetMute(id, min)) { server.send(404, "text/plain", "nie ma celu " + id); return; }
    String name; { Lock l; name = findTarget(id)->name; }
    logEvent(a == "mute" ? "Wyciszono cel " + name + " z panelu " + fmtMute(muteUntilFromMinutes(min))
                         : "Odciszono cel " + name + " z panelu");
  } else if (a == "mute") {
    long min = server.arg("min").toInt(); if (min == 0) min = 60;
    { Lock l; gMutedUntil = muteUntilFromMinutes(min); }
    logEvent("Wyciszono wszystko z panelu " + fmtMute(gMutedUntil));
  } else if (a == "unmute") { { Lock l; gMutedUntil = 0; } logEvent("Odciszono wszystko z panelu"); }
  else if ((a == "enable" || a == "disable") && server.hasArg("id")) {
    String id = server.arg("id");
    String name; { Lock l; Target* t = findTarget(id); if (t) name = t->name; }
    if (name.isEmpty() || !setTargetEnabled(id, a == "enable")) { server.send(404, "text/plain", "nie ma celu " + id); return; }
    logEvent(String(a == "enable" ? "Włączono" : "Wyłączono") + " monitorowanie celu " + name + " z panelu");
    if (a == "enable") runSweepNow();
  }
  else if (a == "test") { tgEnqueue("✅ Test z panelu: Strażnik odpowiada."); logEvent("Test Telegrama z panelu"); }
  // Deliberately hangs the web task past the watchdog: verifies the black box end to end.
  else if (a == "wdttest") { server.send(200, "text/plain", "zawieszam panel na 70 s"); logEvent("Test watchdoga z panelu"); delay(70000); }
  else if (a == "reboot") { server.send(200, "text/plain", "restart"); delay(300); ESP.restart(); }
  else { server.send(400, "text/plain", "nieznana akcja"); return; }
  server.send(200, "text/plain", "ok");
}

static void handleSettings() {
  if (!auth()) return;
  bool wifiChanged = false;
  {
    Lock l;
    String ssid = server.arg("ssid"); ssid.trim();
    if (ssid.length() && ssid != gSet.wifiSsid) { gSet.wifiSsid = ssid; wifiChanged = true; }
    if (server.arg("pass").length()) { gSet.wifiPass = server.arg("pass"); wifiChanged = true; }
    String tok = server.arg("tgtoken"); tok.trim();
    if (tok.length()) gSet.tgToken = tok;
    if (server.hasArg("tgchat")) { String chat = server.arg("tgchat"); chat.trim(); gSet.tgChat = chat; }
    if (server.hasArg("interval")) { long iv = server.arg("interval").toInt(); if (iv >= 10 && iv <= 3600) gSet.interval = iv; }
    if (server.hasArg("faillimit")) { long fl = server.arg("faillimit").toInt(); if (fl >= 1 && fl <= 20) gSet.failLimit = fl; }
    bool mqChanged = false;
    if (server.hasArg("mqhost")) { String h = server.arg("mqhost"); h.trim(); mqChanged |= h != gSet.mqttHost; gSet.mqttHost = h; }
    if (server.hasArg("mqport")) { long p = server.arg("mqport").toInt(); if (p > 0 && p < 65536) { mqChanged |= p != gSet.mqttPort; gSet.mqttPort = p; } }
    if (server.hasArg("mquser")) { String u = server.arg("mquser"); u.trim(); mqChanged |= u != gSet.mqttUser; gSet.mqttUser = u; }
    if (server.arg("mqpass").length()) { gSet.mqttPass = server.arg("mqpass"); mqChanged = true; }
    if (mqChanged && gMqtt.connected()) gMqtt.disconnect();   // mqttTask reconnects with the new settings
    if (server.hasArg("rtgt")) { String v = server.arg("rtgt"); v.trim(); gSet.rescueTarget = v; }
    if (server.hasArg("rhung")) { long v = server.arg("rhung").toInt(); if (v >= 1 && v <= 240) gSet.rescueHungMin = v; }
    auto parseHm = [](const String& v, uint16_t& out) {
      int c = v.indexOf(':'); if (c < 1) return;
      int h = v.substring(0, c).toInt(), m = v.substring(c + 1).toInt();
      if (h >= 0 && h < 24 && m >= 0 && m < 60) out = h * 60 + m;
    };
    if (server.hasArg("rwins")) parseHm(server.arg("rwins"), gSet.rescueWinStart);
    if (server.hasArg("rwine")) parseHm(server.arg("rwine"), gSet.rescueWinEnd);
    if (server.hasArg("rautomin")) { long v = server.arg("rautomin").toInt(); if (v >= 5 && v <= 1440) gSet.rescueAutoMin = v; }
    if (server.hasArg("rauto")) {
      bool v = server.arg("rauto") == "1";
      if (v != gSet.rescueAuto) logEvent(String("Automatyczny restart serwera ") + (v ? "WŁĄCZONY" : "wyłączony") + " z panelu");
      gSet.rescueAuto = v;
    }
    if (server.hasArg("rdry")) {
      bool v = server.arg("rdry") == "1";
      if (v != gSet.rescueDryRun) {
        logEvent(v ? "Restart serwera: tryb TESTOWY" : "Restart serwera: tryb PRAWDZIWY (reset idzie do BMC)");
        tgEnqueue(v ? "🧪 Restart serwera przełączony w tryb testowy — reset nie będzie wysyłany do BMC."
                    : "⚠️ Restart serwera przełączony w tryb PRAWDZIWY — od teraz reset idzie do BMC.");
      }
      gSet.rescueDryRun = v;
    }
    if (server.hasArg("iphost")) { String v = server.arg("iphost"); v.trim(); gSet.ipmiHost = v; }
    if (server.hasArg("ipuser")) { String v = server.arg("ipuser"); v.trim(); gSet.ipmiUser = v; }
    if (server.arg("ippass").length()) gSet.ipmiPass = server.arg("ippass");
    if (server.hasArg("remind")) { long r = server.arg("remind").toInt(); if (r >= 0 && r <= 1440) gSet.remindMin = r; }
    if (server.hasArg("hb")) { long hb = server.arg("hb").toInt(); if (hb >= -1 && hb <= 23) gSet.heartbeatHour = hb; }
    saveSettings();
  }
  logEvent("Zapisano ustawienia z panelu");
  if (wifiChanged) {
    server.send(200, "text/plain", "Zapisano. Restart i łączenie z nową siecią…");
    delay(500); ESP.restart();
  }
  server.send(200, "text/plain", "Zapisano.");
}

static void handleAdopt() {
  if (!auth()) return;
  {
    Lock l;
    if (gTgPendingChat.isEmpty()) { server.send(400, "text/plain", "brak czatu do przyjęcia"); return; }
    gSet.tgChat = gTgPendingChat;
    gTgPendingChat = ""; gTgPendingName = "";
    saveSettings();
  }
  logEvent("Przyjęto czat Telegrama " + gSet.tgChat);
  tgEnqueue("👋 Strażnik sieci podłączony. Będę tu pisał, gdy coś przestanie odpowiadać. /help — komendy.");
  server.send(200, "text/plain", "ok");
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { if (!auth()) return;
    // new firmware = new page; never let the browser keep an old copy
    server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/tglog", HTTP_GET, handleTgLog);
  server.on("/api/target", HTTP_GET, handleTarget);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/adopt-chat", HTTP_POST, handleAdopt);
  server.on("/api/ipmi", HTTP_POST, [] {
    if (!auth()) return;
    String op = server.arg("op"), err;
    if (op == "status") {
      IpmiPower p = ipmiPowerStatus(err);
      { Lock l; gRescue.power = p; gRescue.powerErr = err; gRescue.lastPowerCheck = time(nullptr); }
      server.send(p == IPMI_ERR ? 502 : 200, "text/plain", p == IPMI_ON ? "włączone" : p == IPMI_OFF ? "WYŁĄCZONE" : "błąd: " + err);
    } else if (op == "reset") {
      bool ok = rescueDoReset("ręcznie z panelu");
      server.send(ok ? 200 : 502, "text/plain", ok ? "reset wysłany" : "reset nieudany (szczegóły w dzienniku)");
    } else if (op == "simulate") {
      String msg;
      bool ok = rescueSimulate(server.arg("on") == "1", msg);
      if (ok && server.arg("on") == "1") { tgEnqueue("🧪 Symulacja awarii serwera uruchomiona z panelu (tryb testowy — bez prawdziwego resetu)."); runSweepNow(); }
      server.send(ok ? 200 : 409, "text/plain", msg);
    } else if (op == "on") {
      bool ok = rescuePowerOn(err);
      server.send(ok ? 200 : 502, "text/plain", ok ? "wysłano włączenie" : "błąd: " + err);
    } else server.send(400, "text/plain", "nieznana operacja");
  });
  server.on("/api/targets", HTTP_GET, [] { if (auth()) server.send(200, "application/json", targetsToJson()); });
  server.on("/api/targets", HTTP_POST, [] {
    if (!auth()) return;
    String err;
    if (saveTargets(server.arg("plain"), err)) {
      logEvent("Zapisano nową listę celów (" + String(gTargets.size()) + ")");
      runSweepNow();
      server.send(200, "text/plain", "Zapisano " + String(gTargets.size()) + " celów.");
    } else server.send(400, "text/plain", err);
  });
  server.onNotFound([] { server.send(404, "text/plain", "nie ma"); });
  updater.setup(&server, "/update", "admin", PANEL_PASS);
  server.begin();
}
