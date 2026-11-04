// MQTT + Home Assistant discovery. Optional extra channel: Telegram keeps working
// when the broker (or the whole server it runs on) is down.
//
// Broker account `straznik` has an ACL: it may only write `straznik/#` and
// `homeassistant/+/straznik/#`, and read `homeassistant/status`.
//
// Topics:
//   straznik/status                  online / offline (LWT, retained)
//   straznik/summary                 JSON {up, down, total, muted, rssi, uptime} (retained)
//   straznik/cel/<id>/state          ON = responds, OFF = down (retained)
//   straznik/cel/<id>/attr           JSON {ms, err, since, muted, enabled} (retained)
//   straznik/cmd/sweep               any payload = check everything now
//   straznik/cmd/mute                ON = mute everything until unmuted, OFF = unmute
#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include "state.h"
#include "monitor.h"
#include "net.h"

WiFiClient gMqttNet;
PubSubClient gMqtt(gMqttNet);
volatile bool gMqttRediscover = true;   // (re)send discovery: on connect, list change, HA restart
String gMqttStatus = "wyłączone";
std::vector<String> gMqttPublishedIds;  // targets that have discovery entries, to remove stale ones

static String devId() { String m = WiFi.macAddress(); m.replace(":", ""); m.toLowerCase(); return "straznik_" + m; }

static void addDevice(JsonDocument& d) {
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"][0] = devId();
  dev["name"] = "Strażnik sieci";
  dev["model"] = "ESP32-S3-DevKitC";
  dev["manufacturer"] = "DIY";
  dev["sw_version"] = FW_VERSION;
  dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
  d["availability_topic"] = "straznik/status";
}

static void pubJson(const String& topic, JsonDocument& d, bool retain = true) {
  String s; serializeJson(d, s);
  gMqtt.publish(topic.c_str(), (const uint8_t*)s.c_str(), s.length(), retain);
}

static void publishDiscovery() {
  std::vector<std::pair<String, String>> targets;
  { Lock l; for (auto& t : gTargets) if (t.type != P_SEP) targets.push_back({t.id, t.name}); }

  for (auto& t : targets) {
    JsonDocument d;
    d["name"] = t.second;
    d["unique_id"] = "straznik_" + t.first;
    d["object_id"] = "straznik_" + t.first;
    d["device_class"] = "connectivity";
    d["state_topic"] = "straznik/cel/" + t.first + "/state";
    d["json_attributes_topic"] = "straznik/cel/" + t.first + "/attr";
    addDevice(d);
    pubJson("homeassistant/binary_sensor/straznik/" + t.first + "/config", d);
    esp_task_wdt_reset();
  }
  // Targets deleted from the list since the last discovery: remove their entities.
  for (auto& old : gMqttPublishedIds) {
    bool still = false;
    for (auto& t : targets) if (t.first == old) still = true;
    if (!still) {
      gMqtt.publish(("homeassistant/binary_sensor/straznik/" + old + "/config").c_str(), "", true);
      gMqtt.publish(("straznik/cel/" + old + "/state").c_str(), "", true);
      gMqtt.publish(("straznik/cel/" + old + "/attr").c_str(), "", true);
    }
  }
  gMqttPublishedIds.clear();
  for (auto& t : targets) gMqttPublishedIds.push_back(t.first);

  struct S { const char* key; const char* name; const char* tpl; const char* unit; const char* icon; };
  const S sensors[] = {
    {"up",     "Cele działające",  "{{ value_json.up }}",     nullptr, "mdi:check-network"},
    {"down",   "Cele leżące",      "{{ value_json.down }}",   nullptr, "mdi:close-network"},
    {"rssi",   "Wi-Fi sygnał",     "{{ value_json.rssi }}",   "dBm",   "mdi:wifi"},
    {"uptime", "Czas działania",   "{{ value_json.uptime }}", "s",     "mdi:timer-outline"},
  };
  for (auto& s : sensors) {
    JsonDocument d;
    d["name"] = s.name;
    d["unique_id"] = String("straznik_") + s.key;
    d["object_id"] = String("straznik_") + s.key;
    d["state_topic"] = "straznik/summary";
    d["value_template"] = s.tpl;
    if (s.unit) d["unit_of_measurement"] = s.unit;
    if (!strcmp(s.key, "rssi")) { d["device_class"] = "signal_strength"; d["entity_category"] = "diagnostic"; }
    if (!strcmp(s.key, "uptime")) { d["device_class"] = "duration"; d["entity_category"] = "diagnostic"; }
    if (!strcmp(s.key, "up") || !strcmp(s.key, "down")) d["state_class"] = "measurement";
    d["icon"] = s.icon;
    addDevice(d);
    pubJson(String("homeassistant/sensor/straznik/") + s.key + "/config", d);
  }
  {
    JsonDocument d;
    d["name"] = "Sprawdź teraz";
    d["unique_id"] = "straznik_sweep"; d["object_id"] = "straznik_sweep";
    d["command_topic"] = "straznik/cmd/sweep";
    d["icon"] = "mdi:refresh";
    addDevice(d);
    pubJson("homeassistant/button/straznik/sweep/config", d);
  }
  {
    JsonDocument d;
    d["name"] = "Wycisz powiadomienia";
    d["unique_id"] = "straznik_mute"; d["object_id"] = "straznik_mute";
    d["command_topic"] = "straznik/cmd/mute";
    d["state_topic"] = "straznik/summary";
    d["value_template"] = "{{ 'ON' if value_json.muted else 'OFF' }}";
    d["icon"] = "mdi:bell-off";
    addDevice(d);
    pubJson("homeassistant/switch/straznik/mute/config", d);
  }
}

static void publishStates() {
  JsonDocument sum;
  struct Row { String id; bool up, known; JsonDocument attr; };
  std::vector<Row> rows;
  {
    Lock l;
    int up = 0, down = 0, total = 0;
    for (auto& t : gTargets) {
      if (t.type == P_SEP) continue;
      Row r; r.id = t.id; r.known = t.enabled && t.state != T_UNKNOWN; r.up = t.state == T_UP;
      r.attr["enabled"] = t.enabled;
      r.attr["ms"] = t.lastMs;
      r.attr["err"] = t.lastErr;
      r.attr["since"] = fmtTime(t.since);
      r.attr["muted"] = targetMuted(t);
      r.attr["host"] = t.host;
      rows.push_back(std::move(r));
      if (!monitored(t)) continue;
      total++; up += t.state == T_UP; down += t.state == T_DOWN;
    }
    sum["up"] = up; sum["down"] = down; sum["total"] = total;
    sum["muted"] = isMuted();
  }
  sum["rssi"] = WiFi.RSSI();
  sum["uptime"] = millis() / 1000;
  pubJson("straznik/summary", sum);
  for (auto& r : rows) {
    // Disabled or not yet checked: empty state = "unknown" in HA, not a false "down".
    gMqtt.publish(("straznik/cel/" + r.id + "/state").c_str(), r.known ? (r.up ? "ON" : "OFF") : "None", true);
    pubJson("straznik/cel/" + r.id + "/attr", r.attr);
    esp_task_wdt_reset();
  }
}

static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String t = topic, p;
  for (unsigned i = 0; i < len; i++) p += (char)payload[i];
  if (t == "homeassistant/status" && p == "online") { gMqttRediscover = true; return; }  // HA restarted
  if (t == "straznik/cmd/sweep") { runSweepNow(); logEvent("Sprawdzenie na żądanie z Home Assistanta"); return; }
  if (t == "straznik/cmd/mute") {
    bool on = p == "ON";
    { Lock l; gMutedUntil = on ? MUTE_FOREVER : 0; }
    logEvent(on ? "Wyciszono wszystko z Home Assistanta" : "Odciszono wszystko z Home Assistanta");
  }
}

void mqttTask(void*) {
  esp_task_wdt_add(nullptr);
  gMqtt.setBufferSize(1536);
  gMqtt.setKeepAlive(30);
  gMqtt.setSocketTimeout(5);
  gMqtt.setCallback(onMqttMessage);
  uint32_t lastTry = 0, lastSweeps = 0, lastPub = 0;
  bool lastMuted = false;
  for (;;) {
    esp_task_wdt_reset();
    crumb(CR_MQTT, "czeka");
    String host, user, pass; uint16_t port;
    { Lock l; host = gSet.mqttHost; user = gSet.mqttUser; pass = gSet.mqttPass; port = gSet.mqttPort; }
    if (host.isEmpty()) {
      if (gMqtt.connected()) gMqtt.disconnect();
      gMqttStatus = "wyłączone";
    } else if (WiFi.isConnected()) {
      if (!gMqtt.connected() && millis() - lastTry > 15000) {
        lastTry = millis();
        IPAddress brokerIp;
        if (!resolveName(host, brokerIp)) { gMqttStatus = "nie rozwiązuję nazwy " + host; vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        gMqtt.setServer(brokerIp, port);
        crumb(CR_MQTT, "łączenie");
        if (gMqtt.connect(devId().c_str(), user.c_str(), pass.c_str(), "straznik/status", 1, true, "offline")) {
          gMqtt.publish("straznik/status", "online", true);
          gMqtt.subscribe("straznik/cmd/#");
          gMqtt.subscribe("homeassistant/status");
          gMqttRediscover = true;
          gMqttStatus = "połączone";
          logEvent("MQTT: połączono z " + host);
        } else {
          gMqttStatus = "błąd połączenia (" + String(gMqtt.state()) + ")";
        }
      }
      if (gMqtt.connected()) {
        gMqtt.loop();
        if (gMqttRediscover || gTargetsDirty) {
          gMqttRediscover = false; gTargetsDirty = false;
          crumb(CR_MQTT, "discovery");
          publishDiscovery();
          lastSweeps = 0;                       // force a state refresh right after
        }
        uint32_t sw; { Lock l; sw = gSweeps; }
        bool muted = isMuted();   // mute changed from panel/Telegram/HA: HA switch must follow at once
        if (sw != lastSweeps || muted != lastMuted || millis() - lastPub > 60000) {
          lastSweeps = sw; lastPub = millis(); lastMuted = muted;
          crumb(CR_MQTT, "publikacja stanów");
          publishStates();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
