// Strażnik sieci — independent LAN monitor. Probes hosts/services on the LAN
// and reports outages to Telegram. Test version: Wi-Fi only (Ethernet, GSM later).
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "lwip/dns.h"
#include "config.h"
#include "net.h"
#include "esp_core_dump.h"
#include "state.h"
#include "probes.h"
#include "telegram.h"
#include "monitor.h"
#include "ipmi.h"
#include "rescue.h"
#include "mqtt.h"
#include "webui.h"

// Without these two passwords the panel would be open to anyone and a monitor
// that cannot join Wi-Fi would be impossible to reconfigure — refuse to build.
static_assert(sizeof(PANEL_PASS) > 1, "secrets.h: wpisz PANEL_PASS (haslo panelu WWW)");
static_assert(sizeof(SETUP_AP_PASS) >= 9, "secrets.h: SETUP_AP_PASS musi miec co najmniej 8 znakow");

static uint32_t wifiLostAt = 0;       // millis() when STA went down, 0 = connected
static bool apActive = false;
static uint32_t lastReconnect = 0;
static bool everConnected = false;  // no "lost Wi-Fi" noise before the first connection

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "włączenie zasilania";
    case ESP_RST_SW:       return "restart programowy";
    case ESP_RST_PANIC:    return "błąd programu (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog — program się zawiesił";
    case ESP_RST_BROWNOUT: return "spadek napięcia zasilania";
    case ESP_RST_EXT:      return "przycisk RESET";
    default:               return "inna";
  }
}

// The monitor must keep resolving names when the local DNS (e.g. AdGuard) is down.
// A panic leaves an ELF core dump in its own flash partition. Summarise it once
// (task, address, cause, backtrace), then erase it. Decode the addresses with
// tools/decode_crash.sh <version> <addr...> — build.sh keeps the ELF of every version.
static String crashReport() {
  if (esp_core_dump_image_check() != ESP_OK) return "";
  esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
  String out;
  if (s && esp_core_dump_get_summary(s) == ESP_OK) {
    char b[48];
    snprintf(b, sizeof b, "0x%08lx", (unsigned long)s->exc_pc);
    out = String("zadanie „") + s->exc_task + "”, adres " + b;
    snprintf(b, sizeof b, ", przyczyna %lu, adres danych 0x%lx", (unsigned long)s->ex_info.exc_cause, (unsigned long)s->ex_info.exc_vaddr);
    out += b;
    out += ", ślad:";
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 10; i++) {
      snprintf(b, sizeof b, " 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
      out += b;
    }
    if (s->exc_bt_info.corrupted) out += " (ślad uszkodzony)";
  }
  free(s);
  esp_core_dump_image_erase();
  return out;
}

static void forcePublicDns() { setPublicDns(DNS_PRIMARY, DNS_SECONDARY); }

static void onWifiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) forcePublicDns();
}

static void startWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);               // lower latency, fewer false timeouts
  WiFi.onEvent(onWifiEvent);
  String ssid, pass;
  { Lock l; ssid = gSet.wifiSsid; pass = gSet.wifiPass; }
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  else logEvent("Brak skonfigurowanej sieci Wi-Fi");
}

static void wifiWatch() {
  bool up = WiFi.isConnected();
  // A DHCP renewal silently puts the router's DNS back — re-assert ours.
  static uint32_t lastDns = 0;
  if (up && millis() - lastDns > 60000) { lastDns = millis(); forcePublicDns(); }
  if (up) {
    if (!everConnected) {
      everConnected = true;
      wifiLostAt = 0;
      logEvent("Wi-Fi połączone, IP " + WiFi.localIP().toString());
    }
    if (wifiLostAt) {
      uint32_t outage = millis() - wifiLostAt;
      logEvent("Wi-Fi wróciło po " + String(outage / 1000) + " s, IP " + WiFi.localIP().toString());
      if (outage > WIFI_ALERT_MS)
        tgEnqueueStamped("📶 Strażnik nie miał Wi-Fi przez " + fmtDuration(outage / 1000) +
                         " — w tym czasie nic nie sprawdzał.");
      wifiLostAt = 0;
    }
    if (apActive) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive = false; logEvent("Wyłączono AP konfiguracyjny"); }
    return;
  }
  if (!wifiLostAt) {
    wifiLostAt = millis() ? millis() : 1;
    if (everConnected) logEvent("Utracono Wi-Fi");
  }
  if (!apActive && millis() - wifiLostAt > WIFI_AP_AFTER_MS && strlen(SETUP_AP_PASS) >= 8) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);
    apActive = true;
    logEvent("Brak Wi-Fi od minuty — włączono AP „" SETUP_AP_SSID "” (panel: 192.168.4.1)");
  }
  if (millis() - lastReconnect > 30000) {
    lastReconnect = millis();
    String ssid, pass;
    { Lock l; ssid = gSet.wifiSsid; pass = gSet.wifiPass; }
    if (ssid.length()) { WiFi.disconnect(false); WiFi.begin(ssid.c_str(), pass.c_str()); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  gLock = xSemaphoreCreateRecursiveMutex();
  gIpmiLock = xSemaphoreCreateMutex();
  Serial.printf("\n=== Straznik sieci %s ===\n", FW_VERSION);

  if (!LittleFS.begin(true)) Serial.println("LittleFS: montowanie nieudane");
  loadSettings();
  loadTargets();
  esp_reset_reason_t rr = esp_reset_reason();
  String box = crumbsReport(rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT);
  for (int i = 0; i < CR_N; i++) crumb((CrumbTask)i, "start");
  logEvent(String("Start, firmware ") + FW_VERSION + ", przyczyna: " + resetReason());
  if (box.length()) logEvent("Czarna skrzynka: " + box);
  String crash = crashReport();
  if (crash.length()) logEvent("Zrzut awarii: " + crash);

  startWifi();
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "tempus1.gum.gov.pl");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  setupWeb();

  tgEnqueue(String("🛡️ Strażnik wystartował (fw ") + FW_VERSION + ", przyczyna: " + resetReason() +
            "). Pilnuję " + String(gTargets.size()) + " celów." + (box.length() ? "\nCzarna skrzynka: " + box + "." : "") +
            (crash.length() ? "\nZrzut awarii (fw " FW_VERSION " lub wcześniejszy): " + crash : ""));

  esp_err_t wdt = esp_task_wdt_init(WDT_TIMEOUT_S, true);
  if (wdt != ESP_OK) logEvent("Watchdog: konfiguracja nieudana (" + String(esp_err_to_name(wdt)) + ")");
  esp_task_wdt_add(nullptr);          // loop task
  xTaskCreatePinnedToCore(monitorTask, "monitor", 12288, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(telegramTask, "telegram", 12288, nullptr, 1, nullptr, 1);
}

void loop() {
  esp_task_wdt_reset();
  crumb(CR_WEB, "obsługa panelu");
  server.handleClient();
  static uint32_t lastWatch = 0;
  if (millis() - lastWatch > 1000) { lastWatch = millis(); wifiWatch(); }
  delay(2);
}
