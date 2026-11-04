// Shared state: settings, targets, event log. Everything touched by more than
// one task goes through `gLock`.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <deque>
#include <time.h>
#include "config.h"
#include "secrets.h"

enum ProbeType { P_PING, P_TCP, P_HTTP, P_DNS, P_SEP };   // P_SEP = visual section header, never probed
enum TargetState { T_UNKNOWN, T_UP, T_DOWN };

struct Target {
  String id, name, host, path, query, parent;
  ProbeType type = P_PING;
  uint16_t port = 0;
  int expect = 0;           // HTTP: required status code, 0 = any answer below 500
  bool tls = false;         // HTTP over TLS
  bool enabled = true;
  uint8_t every = 1;        // probe only every N-th sweep (public sites: 10 = every 5 min)
  String needs;             // extra dependencies besides `parent`, comma-separated ids (e.g. "npm,internet")
  time_t mutedUntil = 0;    // no Telegram messages for this target until then (persisted in /mutes.json)
  // runtime
  TargetState state = T_UNKNOWN;
  uint8_t fails = 0;
  bool alerted = false;     // a DOWN message went out for the current outage
  time_t since = 0;         // when the current state began
  uint32_t lastMs = 0;      // last response time
  String lastErr;
  int16_t certDays = -1;    // HTTPS: days until the server certificate expires (-1 = unknown)
  uint32_t fullMs = 0;      // HTTPS: whole request incl. the ESP's own TLS handshake (lastMs = TCP connect only)
  time_t lastNotify = 0;    // last DOWN alert or reminder sent for this outage
  // last HIST_LEN probe results for the panel sparkline (ms, 0xFFFF = no answer)
  uint16_t hist[60];
  uint8_t histPos = 0, histLen = 0;
  void pushHist(bool ok, uint32_t ms) {
    hist[histPos] = ok ? (uint16_t)min<uint32_t>(ms, 65534) : 0xFFFF;
    histPos = (histPos + 1) % 60;
    if (histLen < 60) histLen++;
  }
};

struct Settings {
  String wifiSsid, wifiPass, tgToken, tgChat;
  uint16_t interval = DEF_INTERVAL_S;
  uint8_t failLimit = DEF_FAIL_LIMIT;
  int8_t heartbeatHour = DEF_HEARTBEAT_H;
  uint16_t remindMin = DEF_REMIND_MIN;
  String mqttHost, mqttUser, mqttPass;
  uint16_t mqttPort = 1883;
  String ipmiHost, ipmiUser, ipmiPass;
  String rescueTarget = "serwer";   // target that the IPMI host represents
  bool rescueAuto = false;         // automatic reset when nobody answers
  bool rescueDryRun = true;        // test mode: full procedure + Telegram, but nothing sent to the BMC
  uint16_t rescueHungMin = 5;      // down this long with power ON = hung
  uint16_t rescueAutoMin = 30;     // auto reset after this long without an answer
  uint16_t rescueWinStart = 23 * 60 + 30;   // auto reset allowed only in this local-time window
  uint16_t rescueWinEnd = 7 * 60;           // (minutes after midnight; start == end = all day)
};

struct LogEntry { time_t t; String msg; };

SemaphoreHandle_t gLock;
Settings gSet;
std::vector<Target> gTargets;
std::deque<LogEntry> gLog;
time_t gMutedUntil = 0;
uint32_t gSweeps = 0;
time_t gLastSweep = 0;
bool gTargetsDirty = false;     // targets replaced from the panel, monitor re-reads

struct Lock {
  Lock() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(gLock); }
};

bool monitored(const struct Target& t);
// ---------- black box ----------
// Each task writes what it is doing into RTC memory that survives a watchdog reset.
// After such a reset, the task with the oldest heartbeat is the one that hung.
enum CrumbTask { CR_WEB, CR_MONITOR, CR_TELEGRAM, CR_MQTT, CR_N };
static const char* const CRUMB_NAMES[CR_N] = {"panel WWW", "sprawdzanie", "Telegram", "MQTT"};
struct Crumbs { uint32_t magic; uint32_t ms[CR_N]; char stage[CR_N][24]; };
RTC_NOINIT_ATTR Crumbs gCrumbs;
const uint32_t CRUMB_MAGIC = 0x57A2D1C5;
void crumb(CrumbTask t, const char* stage) {
  gCrumbs.ms[t] = millis();
  strncpy(gCrumbs.stage[t], stage, sizeof gCrumbs.stage[t] - 1);
  gCrumbs.stage[t][sizeof gCrumbs.stage[t] - 1] = 0;
}
// Called once at boot, before any crumb() — reads the previous run's box, then clears it.
String crumbsReport(bool watchdogReset) {
  String out;
  if (watchdogReset && gCrumbs.magic == CRUMB_MAGIC) {
    int worst = -1; uint32_t last = UINT32_MAX, newest = 0;
    for (int i = 0; i < CR_N; i++) { newest = max(newest, gCrumbs.ms[i]); if (gCrumbs.ms[i] < last) { last = gCrumbs.ms[i]; worst = i; } }
    if (worst >= 0) {
      gCrumbs.stage[worst][23] = 0;
      out = String("zawiesiło się zadanie „") + CRUMB_NAMES[worst] + "” na etapie „" + gCrumbs.stage[worst] +
            "” (" + String((newest - last) / 1000) + " s bez znaku życia; działało " + String(newest / 1000 / 60) + " min)";
    }
  }
  memset(&gCrumbs, 0, sizeof gCrumbs);
  gCrumbs.magic = CRUMB_MAGIC;
  return out;
}

bool timeValid() { return time(nullptr) > 1700000000; }

String fmtTime(time_t t) {
  if (t < 1700000000) return String("--");
  struct tm tm; localtime_r(&t, &tm);
  char b[24]; strftime(b, sizeof b, "%d.%m %H:%M:%S", &tm);
  return b;
}

String fmtDuration(time_t s) {
  if (s < 0) s = 0;
  char b[32];
  if (s < 3600) snprintf(b, sizeof b, "%ld min", (long)(s / 60));
  else if (s < 86400) snprintf(b, sizeof b, "%ld h %ld min", (long)(s / 3600), (long)(s % 3600 / 60));
  else snprintf(b, sizeof b, "%ld d %ld h", (long)(s / 86400), (long)(s % 86400 / 3600));
  return b;
}

void logEvent(const String& msg) {
  Lock l;
  gLog.push_back({time(nullptr), msg});
  while (gLog.size() > LOG_SIZE) gLog.pop_front();
  Serial.printf("[%s] %s\n", fmtTime(time(nullptr)).c_str(), msg.c_str());
}

bool isMuted() { return gMutedUntil > time(nullptr); }

const time_t MUTE_FOREVER = 2147000000;   // ~2038: "until unmuted" (time_t is 32-bit on this core)

// minutes < 0 = until unmuted, 0 = unmute, > 0 = for that long
time_t muteUntilFromMinutes(long minutes) {
  if (minutes < 0) return MUTE_FOREVER;
  if (minutes == 0) return 0;
  return time(nullptr) + minutes * 60;
}

String fmtMute(time_t until) { return until >= MUTE_FOREVER ? String("do odwołania") : "do " + fmtTime(until); }

// ---------- settings (NVS) ----------

void loadSettings() {
  Preferences p; p.begin("straznik", true);
  gSet.wifiSsid = p.getString("ssid", DEFAULT_WIFI_SSID);
  gSet.wifiPass = p.getString("pass", DEFAULT_WIFI_PASS);
  gSet.tgToken = p.getString("tgtok", DEFAULT_TG_TOKEN);
  gSet.tgChat = p.getString("tgchat", DEFAULT_TG_CHAT);
  gSet.interval = p.getUShort("intv", DEF_INTERVAL_S);
  gSet.failLimit = p.getUChar("flim", DEF_FAIL_LIMIT);
  gSet.heartbeatHour = p.getChar("hb", DEF_HEARTBEAT_H);
  gSet.remindMin = p.getUShort("rem", DEF_REMIND_MIN);
  gSet.mqttHost = p.getString("mqhost", DEFAULT_MQTT_HOST);
  gSet.mqttPort = p.getUShort("mqport", 1883);
  gSet.mqttUser = p.getString("mquser", DEFAULT_MQTT_USER);
  gSet.mqttPass = p.getString("mqpass", DEFAULT_MQTT_PASS);
  gSet.ipmiHost = p.getString("iphost", DEFAULT_IPMI_HOST);
  gSet.ipmiUser = p.getString("ipuser", DEFAULT_IPMI_USER);
  gSet.ipmiPass = p.getString("ippass", DEFAULT_IPMI_PASS);
  gSet.rescueTarget = p.getString("rtgt", "serwer");
  gSet.rescueAuto = p.getBool("rauto", false);
  gSet.rescueDryRun = p.getBool("rdry", true);
  gSet.rescueHungMin = p.getUShort("rhung", 5);
  gSet.rescueAutoMin = p.getUShort("rautomin", 30);
  gSet.rescueWinStart = p.getUShort("rwins", 23 * 60 + 30);
  gSet.rescueWinEnd = p.getUShort("rwine", 7 * 60);
  p.end();
  if (gSet.interval < 10) gSet.interval = 10;
  if (gSet.failLimit < 1) gSet.failLimit = 1;
}

void saveSettings() {
  Preferences p; p.begin("straznik", false);
  p.putString("ssid", gSet.wifiSsid);
  p.putString("pass", gSet.wifiPass);
  p.putString("tgtok", gSet.tgToken);
  p.putString("tgchat", gSet.tgChat);
  p.putUShort("intv", gSet.interval);
  p.putUChar("flim", gSet.failLimit);
  p.putChar("hb", gSet.heartbeatHour);
  p.putUShort("rem", gSet.remindMin);
  p.putString("mqhost", gSet.mqttHost);
  p.putUShort("mqport", gSet.mqttPort);
  p.putString("mquser", gSet.mqttUser);
  p.putString("mqpass", gSet.mqttPass);
  p.putString("iphost", gSet.ipmiHost);
  p.putString("ipuser", gSet.ipmiUser);
  p.putString("ippass", gSet.ipmiPass);
  p.putString("rtgt", gSet.rescueTarget);
  p.putBool("rauto", gSet.rescueAuto);
  p.putBool("rdry", gSet.rescueDryRun);
  p.putUShort("rhung", gSet.rescueHungMin);
  p.putUShort("rautomin", gSet.rescueAutoMin);
  p.putUShort("rwins", gSet.rescueWinStart);
  p.putUShort("rwine", gSet.rescueWinEnd);
  p.end();
}

// ---------- targets (LittleFS /targets.json) ----------

// Starting list used until the user saves their own in the panel. Example addresses —
// every home network is different, so the panel's target editor is the real config.
static const char DEFAULT_TARGETS[] PROGMEM = R"JSON([
 {"id":"sek-siec","name":"Sieć","type":"separator"},
 {"id":"router","name":"Router","type":"ping","host":"192.168.1.1"},
 {"id":"internet","name":"Internet (DNS 1.1.1.1)","type":"dns","host":"1.1.1.1","query":"api.telegram.org","parent":"router"},
 {"id":"sek-serwer","name":"Serwer","type":"separator"},
 {"id":"serwer","name":"Serwer domowy","type":"tcp","host":"192.168.1.10","port":22,"parent":"router"},
 {"id":"ha","name":"Home Assistant","type":"http","host":"192.168.1.11","port":8123,"path":"/manifest.json","expect":200,"parent":"serwer"}
])JSON";

const char* typeName(ProbeType t) {
  switch (t) { case P_TCP: return "tcp"; case P_HTTP: return "http"; case P_DNS: return "dns"; case P_SEP: return "separator"; default: return "ping"; }
}

bool parseType(const String& s, ProbeType& out) {
  if (s == "ping") out = P_PING; else if (s == "tcp") out = P_TCP;
  else if (s == "http") out = P_HTTP; else if (s == "dns") out = P_DNS;
  else if (s == "separator") out = P_SEP;
  else return false;
  return true;
}

// Parses and validates a target list; on error returns false with a message.
bool parseTargets(const String& json, std::vector<Target>& out, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = String("JSON: ") + e.c_str(); return false; }
  if (!doc.is<JsonArray>()) { err = "oczekiwana lista [...]"; return false; }
  out.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    Target t;
    t.id = o["id"] | "";
    t.name = o["name"] | t.id.c_str();
    t.host = o["host"] | "";
    t.path = o["path"] | "/";
    t.query = o["query"] | "google.com";
    t.parent = o["parent"] | "";
    t.port = o["port"] | 0;
    t.expect = o["expect"] | 0;
    t.enabled = o["enabled"] | true;
    t.tls = o["tls"] | false;
    t.every = constrain((int)(o["every"] | 1), 1, 120);
    if (o["needs"].is<JsonArray>()) {
      for (JsonVariant n : o["needs"].as<JsonArray>()) { if (t.needs.length()) t.needs += ","; t.needs += n.as<String>(); }
    } else t.needs = o["needs"] | "";
    if (!parseType(o["type"] | "ping", t.type)) { err = "nieznany typ w celu " + t.id; return false; }
    if (t.type == P_SEP) {                       // header only: no host, no parent, id optional
      if (t.id.isEmpty()) t.id = "sep" + String(out.size() + 1);
      if (t.name.isEmpty()) t.name = t.id;
      t.parent = ""; t.host = "";
      for (auto& x : out) if (x.id == t.id) { err = "powtórzone id: " + t.id; return false; }
      out.push_back(t);
      continue;
    }
    if (t.id.isEmpty() || t.host.isEmpty()) { err = "każdy cel musi mieć id i host"; return false; }
    if ((t.type == P_TCP) && !t.port) { err = "tcp bez portu: " + t.id; return false; }
    if (t.type == P_HTTP && !t.port) t.port = t.tls ? 443 : 80;
    for (auto& x : out) if (x.id == t.id) { err = "powtórzone id: " + t.id; return false; }
    out.push_back(t);
    if (out.size() > MAX_TARGETS) { err = "za dużo celów"; return false; }
  }
  // Parents must exist and appear earlier, so one pass evaluates them first.
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i].parent.isEmpty()) continue;
    bool ok = false;
    for (size_t j = 0; j < i; j++) if (out[j].id == out[i].parent && out[j].type != P_SEP) ok = true;
    if (!ok) { err = "rodzic '" + out[i].parent + "' celu " + out[i].id + " musi stać wyżej na liście"; return false; }
  }
  // Extra dependencies ("needs") follow the same rule.
  for (size_t i = 0; i < out.size(); i++) {
    String rest = out[i].needs;
    while (rest.length()) {
      int c = rest.indexOf(','); String id = c < 0 ? rest : rest.substring(0, c); rest = c < 0 ? "" : rest.substring(c + 1);
      id.trim(); if (id.isEmpty()) continue;
      bool ok = false;
      for (size_t j = 0; j < i; j++) if (out[j].id == id && out[j].type != P_SEP) ok = true;
      if (!ok) { err = "zależność '" + id + "' celu " + out[i].id + " musi stać wyżej na liście"; return false; }
    }
  }
  return true;
}

String targetsToJson() {
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  Lock l;
  for (auto& t : gTargets) {
    JsonObject o = a.add<JsonObject>();
    if (t.type == P_SEP) { o["type"] = "separator"; o["id"] = t.id; o["name"] = t.name; continue; }
    o["id"] = t.id; o["name"] = t.name; o["type"] = typeName(t.type); o["host"] = t.host;
    if (t.port) o["port"] = t.port;
    if (t.type == P_HTTP) { o["path"] = t.path; if (t.expect) o["expect"] = t.expect; if (t.tls) o["tls"] = true; }
    if (t.type == P_DNS) o["query"] = t.query;
    if (t.parent.length()) o["parent"] = t.parent;
    if (t.needs.length()) {
      JsonArray n = o["needs"].to<JsonArray>(); String rest = t.needs;
      while (rest.length()) { int c = rest.indexOf(','); n.add(c < 0 ? rest : rest.substring(0, c)); rest = c < 0 ? "" : rest.substring(c + 1); }
    }
    if (t.every > 1) o["every"] = t.every;
    if (!t.enabled) o["enabled"] = false;
  }
  String s; serializeJsonPretty(doc, s);
  return s;
}

Target* findTarget(const String& id);
void loadMutes();

void loadTargets() {
  String json;
  File f = LittleFS.open("/targets.json", "r");
  if (f) { json = f.readString(); f.close(); }
  std::vector<Target> list; String err;
  if (json.length() && parseTargets(json, list, err)) { gTargets = list; loadMutes(); return; }
  if (json.length()) logEvent("Zła lista celów w pamięci (" + err + ") — wczytuję domyślną");
  parseTargets(FPSTR(DEFAULT_TARGETS), list, err);
  gTargets = list;
}

bool saveTargets(const String& json, String& err) {
  std::vector<Target> list;
  if (!parseTargets(json, list, err)) return false;
  File f = LittleFS.open("/targets.json", "w");
  if (!f) { err = "zapis do pamięci nieudany"; return false; }
  f.print(json); f.close();
  Lock l;
  for (auto& n : list) { Target* old = findTarget(n.id); if (old) n.mutedUntil = old->mutedUntil; }
  gTargets = list;
  gTargetsDirty = true;
  return true;
}

Target* findTarget(const String& id) {
  for (auto& t : gTargets) if (t.id == id) return &t;
  return nullptr;
}

bool targetMuted(const Target& t) { return t.mutedUntil > time(nullptr); }

// Per-target mutes live in their own file, so editing the target list keeps them.
void saveMutes() {
  JsonDocument doc;
  {
    Lock l;
    time_t now = time(nullptr);
    for (auto& t : gTargets) if (t.mutedUntil > now) doc[t.id] = (long long)t.mutedUntil;
  }
  File f = LittleFS.open("/mutes.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadMutes() {
  File f = LittleFS.open("/mutes.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (!deserializeJson(doc, f)) {
    Lock l;
    for (JsonPair kv : doc.as<JsonObject>()) {
      Target* t = findTarget(kv.key().c_str());
      if (t) t->mutedUntil = kv.value().as<long long>();
    }
  }
  f.close();
}

bool setTargetMute(const String& id, long minutes) {
  {
    Lock l;
    Target* t = findTarget(id);
    if (!t) return false;
    t->mutedUntil = muteUntilFromMinutes(minutes);
  }
  saveMutes();
  return true;
}

// Takes a target out of monitoring (or back) without deleting it from the list.
// The flag is stored in /targets.json, so it survives reboots and shows in the editor.
bool setTargetEnabled(const String& id, bool on) {
  {
    Lock l;
    Target* t = findTarget(id);
    if (!t) return false;
    t->enabled = on;
    t->state = T_UNKNOWN; t->fails = 0; t->alerted = false;
    t->since = time(nullptr); t->lastErr = ""; t->lastMs = 0;
  }
  String json = targetsToJson();
  File f = LittleFS.open("/targets.json", "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

bool monitored(const Target& t) { return t.enabled && t.type != P_SEP; }
