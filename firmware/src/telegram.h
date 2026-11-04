// Telegram: outgoing queue (survives network outages, retried with backoff)
// and command polling. Only the configured chat is obeyed.
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include "state.h"
#include "ca_telegram.h"
#include "ipmi.h"
#include "net.h"

String statusText(bool onlyProblems);   // monitor.h
void runSweepNow();                     // monitor.h
String rescueButton(const String& action);   // rescue.h
bool rescueSimulate(bool on, String& msg);    // rescue.h

struct TgMsg { String text, markup; };   // markup = inline_keyboard JSON or empty
std::deque<TgMsg> gTgQueue;
String gTgPendingChat, gTgPendingName;  // last unknown chat that wrote to the bot (for setup)
String gTgLastError;

// Message log for the panel: what went out (and whether it arrived) and what came in.
enum TgLogState : uint8_t { TGL_SENT, TGL_RETRY, TGL_DROPPED, TGL_IN };
struct TgLogEntry { time_t t; TgLogState st; String text, err; };
std::deque<TgLogEntry> gTgLog;
void tgLogAdd(TgLogState st, const String& text, const String& err = "") {
  Lock l;
  gTgLog.push_back({time(nullptr), st, text.length() > 400 ? text.substring(0, 400) + "…" : text, err});
  while (gTgLog.size() > 60) gTgLog.pop_front();
}
volatile const char* gTgStage = "start";   // diagnostics: where the Telegram task is
volatile uint32_t gTgLoopMs = 0;
time_t gTgLastOk = 0;

bool tgConfigured() { Lock l; return gSet.tgToken.length() > 0 && gSet.tgChat.length() > 0; }

void tgEnqueue(const String& text, const String& markup = "") {
  Lock l;
  if (gTgQueue.size() >= TG_QUEUE_MAX) gTgQueue.pop_front();
  gTgQueue.push_back({text, markup});
}

// Messages queued while offline get their creation time prefixed, so a report
// delivered an hour late still says when it happened.
void tgEnqueueStamped(const String& text, const String& markup = "") {
  tgEnqueue(timeValid() ? "[" + fmtTime(time(nullptr)) + "] " + text : text, markup);
}

// Inline "mute" buttons under an alert: per target 1 h / until unmuted, plus "everything 1 h".
String muteButtons(const std::vector<std::pair<String, String>>& targets) {
  JsonDocument d;
  JsonArray rows = d["inline_keyboard"].to<JsonArray>();
  size_t n = 0;
  for (auto& t : targets) {
    if (++n > 5) break;                       // keep the keyboard readable
    String name = t.second.length() > 18 ? t.second.substring(0, 17) + "…" : t.second;
    JsonArray row = rows.add<JsonArray>();
    JsonObject a = row.add<JsonObject>(); a["text"] = "🔕 " + name + " 1 h"; a["callback_data"] = "m:" + t.first + ":60";
    JsonObject b = row.add<JsonObject>(); b["text"] = "do odwołania"; b["callback_data"] = "m:" + t.first + ":-1";
  }
  JsonArray row = rows.add<JsonArray>();
  JsonObject all = row.add<JsonObject>(); all["text"] = "🔕 Wszystko na 1 h"; all["callback_data"] = "a:60";
  String s; serializeJson(d, s);
  return s;
}

static String gTgNetErr;   // text of the last network-level failure (negative code)

static int tgRequest(const String& method, const String& body, String& resp) {
  String token;
  { Lock l; token = gSet.tgToken; }
  WiFiClientSecure client;
  client.setCACert(TELEGRAM_ROOT_CA);
  client.setHandshakeTimeout(10);   // core default is 120 s — longer than the 60 s watchdog
  client.setTimeout(8);
  IPAddress ip;
  if (!resolveName("api.telegram.org", ip)) return -100;           // shown as "błąd -100" in the panel
  if (!client.connect(ip, 443, "api.telegram.org", TELEGRAM_ROOT_CA, nullptr, nullptr)) return -101;
  String herr;
  int code = httpExchange(client, "POST", "api.telegram.org", "/bot" + token + "/" + method,
                          "application/json", body, &resp, 10000, herr);
  client.stop();
  if (code <= 0) gTgNetErr = herr;
  return code;
}

static bool tgSend(const String& chat, const TgMsg& msg) {
  JsonDocument d;
  d["chat_id"] = chat;
  d["text"] = msg.text;
  d["disable_web_page_preview"] = true;
  if (msg.markup.length()) d["reply_markup"] = serialized(msg.markup);
  String body; serializeJson(d, body);
  String resp;
  int code = tgRequest("sendMessage", body, resp);
  if (code == 200) { gTgLastOk = time(nullptr); gTgLastError = ""; return true; }
  gTgLastError = code > 0 ? "HTTP " + String(code) + " " + resp.substring(0, 160)
                          : "błąd " + String(code) + " " + (code == -100 ? String("nie rozwiązuję api.telegram.org") : code == -101 ? String("połączenie TLS nieudane") : gTgNetErr);
  logEvent("Telegram: wysyłka nieudana — " + gTgLastError);
  // 400/401/403 = bad token or chat: retrying will not help, drop the message.
  return code == 400 || code == 401 || code == 403;
}

static void tgHandleCommand(const String& chat, String text) {
  text.trim();
  tgLogAdd(TGL_IN, text);
  int at = text.indexOf('@');                 // "/status@botname" in groups
  int sp = text.indexOf(' ');
  String cmd = text.substring(0, sp < 0 ? text.length() : sp);
  if (at > 0 && (sp < 0 || at < sp)) cmd = text.substring(0, at);
  String arg = sp < 0 ? "" : text.substring(sp + 1);
  arg.trim();
  cmd.toLowerCase();

  if (cmd == "/status") {
    tgEnqueue(statusText(false));
  } else if (cmd == "/problemy") {
    tgEnqueue(statusText(true));
  } else if (cmd == "/mute" || cmd == "/wycisz" || cmd == "/unmute" || cmd == "/odcisz") {
    bool mute = cmd == "/mute" || cmd == "/wycisz";
    // "/wycisz 30" = everything; "/wycisz ha 30" or "/wycisz ha" = one target (no time = until unmuted)
    String first = arg, rest;
    int s2 = arg.indexOf(' ');
    if (s2 > 0) { first = arg.substring(0, s2); rest = arg.substring(s2 + 1); rest.trim(); }
    bool numeric = first.length() && (isDigit(first[0]) || first[0] == '-');
    if (first.length() && !numeric) {
      long min = mute ? (rest.length() ? rest.toInt() : -1) : 0;
      if (mute && min == 0) min = -1;
      String name;
      { Lock l; Target* t = findTarget(first); if (t) name = t->name; }
      if (name.isEmpty() || !setTargetMute(first, min)) {
        tgEnqueue("Nie znam celu „" + first + "”. Identyfikatory są w /cele.");
      } else if (mute) {
        logEvent("Wyciszono cel " + name + " z Telegrama " + fmtMute(muteUntilFromMinutes(min)));
        tgEnqueue("🔕 " + name + " wyciszony " + fmtMute(muteUntilFromMinutes(min)) + ".");
      } else {
        logEvent("Odciszono cel " + name + " z Telegrama");
        tgEnqueue("🔔 " + name + " znów zgłasza awarie.");
      }
    } else if (mute) {
      long min = numeric ? first.toInt() : 60;
      if (min == 0) min = 60;
      { Lock l; gMutedUntil = muteUntilFromMinutes(min); }
      logEvent("Wyciszono wszystko z Telegrama " + fmtMute(gMutedUntil));
      tgEnqueue("🔕 Wszystko wyciszone " + fmtMute(gMutedUntil) + ". Awarie dalej trafiają do dziennika.");
    } else {
      { Lock l; gMutedUntil = 0; }
      logEvent("Odciszono wszystko z Telegrama");
      tgEnqueue("🔔 Powiadomienia włączone.\n\n" + statusText(true));
    }
  } else if (cmd == "/cele") {
    String out = "Cele (id — nazwa):\n";
    { Lock l; for (auto& t : gTargets) if (t.type == P_SEP) out += "— " + t.name + " —\n"; else out += String(!t.enabled ? "⏸ " : "") + (targetMuted(t) ? "🔕 " : "") + t.id + " — " + t.name + "\n"; }
    tgEnqueue(out);
  } else if (cmd == "/wylacz" || cmd == "/wlacz") {
    bool on = cmd == "/wlacz";
    String name; { Lock l; Target* t = findTarget(arg); if (t) name = t->name; }
    if (arg.isEmpty() || name.isEmpty() || !setTargetEnabled(arg, on)) {
      tgEnqueue("Nie znam celu „" + arg + "”. Identyfikatory są w /cele.");
    } else {
      logEvent(String(on ? "Włączono" : "Wyłączono") + " monitorowanie celu " + name + " z Telegrama");
      tgEnqueue(on ? "▶️ " + name + " znów monitorowany." : "⏸ " + name + " wyłączony z monitorowania (zostaje na liście, /wlacz " + arg + " przywraca).");
      if (on) runSweepNow();
    }
  } else if (cmd == "/sprawdz") {
    runSweepNow();
    tgEnqueue("🔄 Sprawdzam wszystko teraz — wynik za chwilę w /status.");
  } else if (cmd == "/restart" || cmd == "/reset") {
    rescueButton("reset?");
  } else if (cmd == "/symuluj") {
    String m;
    bool ok = rescueSimulate(arg != "stop", m);
    tgEnqueue(ok ? (arg == "stop" ? "🧪 Symulacja zakończona." : "🧪 Symulacja awarii serwera uruchomiona. Za chwilę przyjdzie pytanie o restart — reset NIE pójdzie do BMC.") : "❌ " + m);
    if (ok && arg != "stop") runSweepNow();
  } else if (cmd == "/ipmi") {
    tgEnqueue("⏳ Pytam BMC…");
    String err; IpmiPower pw = ipmiPowerStatus(err);
    tgEnqueue(pw == IPMI_ON ? "⚡ IPMI: zasilanie serwera WŁĄCZONE." : pw == IPMI_OFF ? "⚫ IPMI: zasilanie serwera WYŁĄCZONE." : "❌ IPMI: " + err);
  } else if (cmd == "/test") {
    tgEnqueue("✅ Test: Strażnik odpowiada. IP " + WiFi.localIP().toString() + ", Wi-Fi " + String(WiFi.RSSI()) + " dBm.");
  } else {
    tgEnqueue("Strażnik sieci — komendy:\n"
              "/status — stan wszystkich celów\n"
              "/problemy — tylko to, co leży\n"
              "/wycisz [min] — wycisz wszystko (domyślnie 60, -1 = do odwołania)\n"
              "/odcisz — włącz wszystko z powrotem\n"
              "/wycisz <id> [min] — wycisz jeden cel (bez czasu = do odwołania)\n"
              "/odcisz <id> — odcisz jeden cel\n"
              "/cele — lista celów z identyfikatorami\n"
              "/wylacz <id> — przestań sprawdzać cel (zostaje na liście)\n"
              "/wlacz <id> — sprawdzaj znów\n"
              "Pod alarmem są przyciski do wyciszenia. Bez wyciszenia strażnik przypomina co "
              + String(gSet.remindMin) + " min, że coś dalej leży.\n"
              "/sprawdz — sprawdź wszystko od razu\n"
              "/ipmi — stan zasilania serwera (BMC)\n"
              "/restart — twardy reset serwera przez IPMI (z potwierdzeniem)\n"
              "/symuluj — test procedury restartu bez resetu (/symuluj stop — koniec)\n"
              "/test — czy strażnik żyje");
  }
}

// A tap on an inline mute button. Only buttons in our own chat are obeyed.
static void tgHandleButton(JsonObject cq, const String& myChat) {
  String chat = String((long long)(cq["message"]["chat"]["id"] | 0LL));
  String data = cq["data"] | "";
  tgLogAdd(TGL_IN, "[przycisk] " + data);
  String reply;
  if (chat != myChat) {
    reply = "Nie Twój strażnik.";
  } else if (data.startsWith("a:")) {
    long min = data.substring(2).toInt();
    { Lock l; gMutedUntil = muteUntilFromMinutes(min); }
    reply = "Wszystko wyciszone " + fmtMute(gMutedUntil);
    logEvent("Wyciszono wszystko przyciskiem w Telegramie " + fmtMute(gMutedUntil));
  } else if (data.startsWith("x:")) {
    reply = rescueButton(data.substring(2));
    JsonDocument d;
    d["callback_query_id"] = cq["id"] | ""; d["text"] = reply;
    String body, resp; serializeJson(d, body);
    tgRequest("answerCallbackQuery", body, resp);
    return;
  } else if (data.startsWith("m:")) {
    int c = data.lastIndexOf(':');
    String id = data.substring(2, c);
    long min = data.substring(c + 1).toInt();
    String name; { Lock l; Target* t = findTarget(id); if (t) name = t->name; }
    if (name.length() && setTargetMute(id, min)) {
      reply = name + " wyciszony " + fmtMute(muteUntilFromMinutes(min));
      logEvent("Wyciszono cel " + name + " przyciskiem w Telegramie " + fmtMute(muteUntilFromMinutes(min)));
    } else reply = "Nie ma już celu " + id;
  }
  JsonDocument d;
  d["callback_query_id"] = cq["id"] | "";
  d["text"] = reply;
  String body, resp; serializeJson(d, body);
  tgRequest("answerCallbackQuery", body, resp);
  if (chat == myChat && reply.length()) tgEnqueue("🔕 " + reply + ".");
}

// Registers the command list, so Telegram shows it in the "/" menu next to the input box.
static bool tgRegisterCommands() {
  static const char* const cmds[][2] = {
    {"status", "Stan wszystkich celów"},
    {"problemy", "Tylko to, co nie działa"},
    {"cele", "Lista celów z identyfikatorami"},
    {"sprawdz", "Sprawdź wszystko teraz"},
    {"wycisz", "Wycisz: /wycisz [min] albo /wycisz <id> [min]"},
    {"odcisz", "Odcisz: /odcisz albo /odcisz <id>"},
    {"wylacz", "Przestań sprawdzać cel: /wylacz <id>"},
    {"wlacz", "Sprawdzaj znów cel: /wlacz <id>"},
    {"ipmi", "Stan zasilania serwera (BMC)"},
    {"restart", "Twardy reset serwera przez IPMI (z potwierdzeniem)"},
    {"symuluj", "Test procedury restartu bez resetu (/symuluj stop — koniec)"},
    {"test", "Czy strażnik żyje"},
    {"help", "Wszystkie komendy"},
  };
  JsonDocument d;
  JsonArray a = d["commands"].to<JsonArray>();
  for (auto& c : cmds) { JsonObject o = a.add<JsonObject>(); o["command"] = c[0]; o["description"] = c[1]; }
  String body, resp; serializeJson(d, body);
  return tgRequest("setMyCommands", body, resp) == 200;
}

static int64_t gTgOffset = 0;
static bool gTgPrimed = false;   // first poll only skips backlog sent before boot

static void tgPoll() {
  // offset -1 on the first poll = only the newest update, which marks the whole backlog as read.
  String body = "{\"offset\":" + String(gTgPrimed ? (long long)gTgOffset : -1LL) + ",\"timeout\":0,\"limit\":5,\"allowed_updates\":[\"message\",\"callback_query\"]}";
  String resp;
  if (tgRequest("getUpdates", body, resp) != 200) return;
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return;
  String myChat;
  { Lock l; myChat = gSet.tgChat; }
  for (JsonObject u : doc["result"].as<JsonArray>()) {
    gTgOffset = (int64_t)(u["update_id"] | 0LL) + 1;
    JsonObject cq = u["callback_query"];
    if (!cq.isNull() && gTgPrimed) { tgHandleButton(cq, myChat); continue; }
    JsonObject m = u["message"];
    if (m.isNull() || !gTgPrimed) continue;
    String chat = String((long long)(m["chat"]["id"] | 0LL));
    String text = m["text"] | "";
    if (chat == myChat) { if (text.startsWith("/")) tgHandleCommand(chat, text); continue; }
    // Unknown chat: never obeyed. Remembered so the panel can offer it as "your chat" during setup.
    String who = String(m["chat"]["first_name"] | "") + " " + String(m["chat"]["username"] | "");
    { Lock l; gTgPendingChat = chat; gTgPendingName = who; }
    logEvent("Wiadomość z nieznanego czatu " + chat + " (" + who + ") — zignorowana");
  }
  if (!gTgPrimed && tgRegisterCommands()) logEvent("Telegram: zarejestrowano menu komend");
  gTgPrimed = true;
}

void telegramTask(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t lastPoll = 0, backoff = 0, nextTry = 0;
  for (;;) {
    esp_task_wdt_reset();
    gTgLoopMs = millis(); gTgStage = "idle"; crumb(CR_TELEGRAM, "czeka");
    bool token;
    { Lock l; token = gSet.tgToken.length() > 0; }
    if (WiFi.isConnected() && token) {
      TgMsg head;
      bool have;
      { Lock l; have = !gTgQueue.empty(); if (have) head = gTgQueue.front(); }
      if (have && tgConfigured() && millis() >= nextTry) {
        String chat; { Lock l; chat = gSet.tgChat; }
        gTgStage = "send"; crumb(CR_TELEGRAM, "wysyłka");
        if (tgSend(chat, head)) {
          // true = done with this message: delivered, or rejected for good (bad token/chat)
          if (gTgLastError.isEmpty()) tgLogAdd(TGL_SENT, head.text);
          else tgLogAdd(TGL_DROPPED, head.text, gTgLastError);
          Lock l; if (!gTgQueue.empty()) gTgQueue.pop_front();
          backoff = 0;
        } else {
          if (!backoff) tgLogAdd(TGL_RETRY, head.text, gTgLastError);   // first failure of this message
          backoff = backoff ? min<uint32_t>(backoff * 2, 300000) : 5000;
          nextTry = millis() + backoff;
        }
        esp_task_wdt_reset();
      }
      if (millis() - lastPoll > TG_POLL_MS) { lastPoll = millis(); gTgStage = "poll"; crumb(CR_TELEGRAM, "odbiór komend"); tgPoll(); }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
