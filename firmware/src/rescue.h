// Emergency restart of the main server through its BMC (IPMI).
//
// The rescue target (default "serwer") is considered HUNG when it has been DOWN for
// `rescueHungMin` minutes while IPMI still reports chassis power ON and its parent
// (the router) answers — i.e. the network is fine, the box is on, the OS is dead.
// Then Telegram gets a message with [Restart] [Wait 30 min] [Don't restart].
// Optional auto mode (off by default, panel switch): nobody answered for
// `rescueAutoMin` minutes -> hard reset. Guards: max one automatic reset per 2 h,
// no second attempt if the server is still down 15 min after a reset.
#pragma once
#include "state.h"
#include "telegram.h"
#include "ipmi.h"

enum RescueState { R_IDLE, R_NOTIFIED, R_HOLD, R_NEVER, R_RESET_DONE };

struct Rescue {
  RescueState st = R_IDLE;
  time_t notifiedAt = 0, holdUntil = 0, resetAt = 0, lastAutoReset = 0, lastPowerCheck = 0;
  IpmiPower power = IPMI_ERR;
  String powerErr, resetBy;
} gRescue;

const time_t AUTO_RESET_GAP_S = 2 * 3600;
// Test mode (gSet.rescueDryRun, persisted, default ON): the whole flow runs and Telegram gets every
// message, but reset / power-on are never sent to the BMC. Power status is still read for real.
#define gRescueDryRun (gSet.rescueDryRun)

// Simulated outage of the rescue target (RAM only, allowed only in test mode): lets the full
// procedure run against the real server and BMC without taking anything down.
time_t gSimStart = 0, gSimEndAt = 0;
static String simTag() { return gSimStart ? "🧪 SYMULACJA — " : ""; }

bool rescueSimulate(bool on, String& msg) {
  if (on && !gRescueDryRun) { msg = "symulacja możliwa tylko w trybie testowym"; return false; }
  Lock l;
  if (on) { gSimStart = time(nullptr) - (time_t)gSet.rescueHungMin * 60; gSimEndAt = 0; time_t keep = gRescue.lastAutoReset; gRescue = Rescue(); gRescue.lastAutoReset = keep; msg = "symulacja awarii uruchomiona"; }
  else { gSimStart = 0; gSimEndAt = 0; gRescue.st = R_IDLE; msg = "symulacja zakończona"; }
  logEvent(msg);
  return true;
}
const time_t RESET_GRACE_S = 15 * 60;

static String hhmm(uint16_t m) { char b[6]; snprintf(b, sizeof b, "%02u:%02u", m / 60, m % 60); return b; }
static String rescueWindowText() { return hhmm(gSet.rescueWinStart) + "–" + hhmm(gSet.rescueWinEnd); }

// Is automatic reset allowed right now? Needs a valid clock; window may cross midnight.
bool rescueInWindow() {
  if (!timeValid()) return false;
  uint16_t s = gSet.rescueWinStart, e = gSet.rescueWinEnd;
  if (s == e) return true;
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  uint16_t m = tm.tm_hour * 60 + tm.tm_min;
  return s < e ? (m >= s && m < e) : (m >= s || m < e);
}

static String rescueName() {
  Lock l;
  Target* t = findTarget(gSet.rescueTarget);
  return t ? t->name : gSet.rescueTarget;
}

static String rescueButtons(bool withAutoInfo) {
  JsonDocument d;
  JsonArray rows = d["inline_keyboard"].to<JsonArray>();
  JsonArray r1 = rows.add<JsonArray>();
  JsonObject a = r1.add<JsonObject>(); a["text"] = "🔄 Zrestartuj"; a["callback_data"] = "x:reset?";
  JsonArray r2 = rows.add<JsonArray>();
  JsonObject b = r2.add<JsonObject>(); b["text"] = "⏳ Czekaj 30 min"; b["callback_data"] = "x:wait";
  JsonObject c = r2.add<JsonObject>(); c["text"] = "✋ Nie restartuj"; c["callback_data"] = "x:never";
  (void)withAutoInfo;
  String s; serializeJson(d, s);
  return s;
}

// Performs the hard reset. `by` goes into the log and the message.
bool rescueDoReset(const String& by) {
  String err;
  String name = rescueName();
  logEvent(String(gRescueDryRun ? "TEST — " : "") + "IPMI: twardy reset " + name + " (" + by + ")");
  if (gRescueDryRun) {
    tgEnqueueStamped(simTag() + "🧪 TRYB TESTOWY: w tym miejscu poszedłby twardy reset " + name + " (" + by + "). Do BMC nic nie wysłano.");
    if (gSimStart) { Lock l; gSimEndAt = time(nullptr) + 60; }   // simulated server "comes back" a minute later
  } else if (!ipmiChassis(CH_RESET, err)) {
    logEvent("IPMI: reset nieudany — " + err);
    tgEnqueueStamped("❌ Reset " + name + " przez IPMI nie udał się: " + err);
    return false;
  }
  {
    Lock l;
    gRescue.st = R_RESET_DONE;
    gRescue.resetAt = time(nullptr);
    gRescue.resetBy = gSimStart ? "sym: " + by : by;
    // a simulated test must not block a real automatic reset for the next 2 h
    if (by == "automatycznie" && !gSimStart) gRescue.lastAutoReset = gRescue.resetAt;
  }
  if (!gRescueDryRun) tgEnqueueStamped("🔄 " + name + ": wysłano twardy reset przez IPMI (" + by + "). Daję mu 15 min na powrót.");
  return true;
}

bool rescuePowerOn(String& err) {
  logEvent(String(gRescueDryRun ? "TEST — " : "") + "IPMI: włączenie zasilania " + rescueName());
  if (gRescueDryRun) return true;
  return ipmiChassis(CH_ON, err);
}

// Telegram buttons "x:..." — returns the short toast text.
String rescueButton(const String& action) {
  String name = rescueName();
  if (action == "reset?") {
    JsonDocument d;
    JsonArray rows = d["inline_keyboard"].to<JsonArray>();
    JsonArray r = rows.add<JsonArray>();
    JsonObject y = r.add<JsonObject>(); y["text"] = "⚠️ TAK, resetuj"; y["callback_data"] = "x:reset!";
    JsonObject n = r.add<JsonObject>(); n["text"] = "Anuluj"; n["callback_data"] = "x:cancel";
    String kb; serializeJson(d, kb);
    tgEnqueue("⚠️ Na pewno zresetować " + name + "? To twardy reset jak przyciskiem RESET — "
              "na chwilę padnie wszystko, co na nim działa.", kb);
    return "Potwierdź reset";
  }
  if (action == "reset!") return rescueDoReset("ręcznie z Telegrama") ? "Reset wysłany" : "Reset nieudany";
  if (action == "cancel") return "Anulowano";
  if (action == "wait") {
    { Lock l; gRescue.st = R_HOLD; gRescue.holdUntil = time(nullptr) + 30 * 60; }
    logEvent("Restart " + name + " wstrzymany na 30 min (Telegram)");
    tgEnqueue(simTag() + "⏳ Czekam 30 min. Potem zapytam znowu" + String(gSet.rescueAuto ? " i odliczanie automatu ruszy od nowa." : "."));
    if (gSimStart) { String m; rescueSimulate(false, m); tgEnqueue("🧪 Symulacja zakończona (wybrano Czekaj)."); }
    return "Czekam 30 min";
  }
  if (action == "never") {
    { Lock l; gRescue.st = R_NEVER; }
    logEvent("Restart " + name + " wyłączony dla tej awarii (Telegram)");
    tgEnqueue(simTag() + "✋ Nie będę restartował " + name + " przy tej awarii. Reset nadal możliwy ręcznie: /restart");
    if (gSimStart) { String m; rescueSimulate(false, m); tgEnqueue("🧪 Symulacja zakończona (wybrano Nie restartuj)."); }
    return "Nie restartuję";
  }
  if (action == "on!") {
    String err;
    bool ok = rescuePowerOn(err);
    tgEnqueue(ok ? "⚡ Wysłano włączenie zasilania " + name + "." : "❌ Włączenie nieudane: " + err);
    return ok ? "Włączam" : "Błąd";
  }
  return "?";
}

// Called by the monitor task after every sweep.
void rescueTick() {
  String id, parentId;
  bool enabled, down = false, parentUp = true;
  time_t since = 0;
  uint16_t hungMin, autoMin;
  bool autoOn;
  {
    Lock l;
    id = gSet.rescueTarget; hungMin = gSet.rescueHungMin; autoMin = gSet.rescueAutoMin; autoOn = gSet.rescueAuto;
    Target* t = id.length() ? findTarget(id) : nullptr;
    enabled = t && monitored(*t) && gSet.ipmiHost.length();
    if (enabled) {
      down = t->state == T_DOWN; since = t->since; parentId = t->parent;
      if (gSimStart) {                         // simulated outage overrides the real state
        if (gSimEndAt && time(nullptr) >= gSimEndAt) { gSimStart = 0; gSimEndAt = 0; }
        else { down = true; since = gSimStart; }
      }
      Target* p = parentId.length() ? findTarget(parentId) : nullptr;
      if (p) parentUp = p->state == T_UP;
    }
  }
  time_t now = time(nullptr);
  String name = rescueName();

  if (!enabled || !down) {
    RescueState was; time_t resetAt; String by;
    { Lock l; was = gRescue.st; resetAt = gRescue.resetAt; by = gRescue.resetBy; gRescue.st = R_IDLE; }
    if (enabled && was == R_RESET_DONE)
      tgEnqueueStamped(String(by.startsWith("sym") ? "🧪 SYMULACJA — " : "") + "✅ " + name + " wstał po resecie (" + (by.startsWith("sym: ") ? by.substring(5) : by) + ") po " + fmtDuration(now - resetAt) + ".");
    return;
  }
  // Router down = we cannot tell anything about the server; never reset blindly.
  if (!parentUp) return;
  if (now - since < (time_t)hungMin * 60) return;

  if (now - gRescue.lastPowerCheck >= 60) {
    String err;
    IpmiPower p = ipmiPowerStatus(err);
    Lock l;
    gRescue.power = p; gRescue.powerErr = err; gRescue.lastPowerCheck = now;
  }

  Rescue r; { Lock l; r = gRescue; }
  switch (r.st) {
    case R_IDLE: {
      String msg;
      String kb;
      if (r.power == IPMI_ON) {
        msg = simTag() + "🔴 " + name + " prawdopodobnie zawieszony: nie odpowiada od " + fmtDuration(now - since) +
              ", a IPMI mówi, że zasilanie jest włączone.";
        if (autoOn) {
          msg += "\nAutomat (" + rescueWindowText() + "): jeśli nikt nie zareaguje, zrestartuję go po " + String(autoMin) + " min";
          msg += rescueInWindow() ? "." : " — ale dopiero w tych godzinach. Teraz decyzja należy do Ciebie.";
        }
        kb = rescueButtons(autoOn);
      } else if (r.power == IPMI_OFF) {
        msg = simTag() + "⚫ " + name + " jest WYŁĄCZONY (IPMI: zasilanie OFF). Nie resetuję — mogę go włączyć.";
        JsonDocument d; JsonArray rows = d["inline_keyboard"].to<JsonArray>();
        JsonObject o = rows.add<JsonArray>().add<JsonObject>(); o["text"] = "⚡ Włącz"; o["callback_data"] = "x:on!";
        serializeJson(d, kb);
      } else {
        msg = simTag() + "🔴 " + name + " nie odpowiada od " + fmtDuration(now - since) +
              ", a BMC (IPMI) też milczy: " + r.powerErr + ". Restart zdalny niemożliwy.";
      }
      tgEnqueueStamped(msg, kb);
      logEvent("Awaria " + name + ", IPMI: " + (r.power == IPMI_ON ? "ON" : r.power == IPMI_OFF ? "OFF" : r.powerErr));
      Lock l; gRescue.st = R_NOTIFIED; gRescue.notifiedAt = now;
      break;
    }
    case R_NOTIFIED:
      if (autoOn && r.power == IPMI_ON && rescueInWindow() && now - r.notifiedAt >= (time_t)autoMin * 60) {
        if (now - r.lastAutoReset < AUTO_RESET_GAP_S) {
          { Lock l; gRescue.st = R_NEVER; }
          tgEnqueueStamped("⚠️ " + name + " znów zawieszony, ale automatyczny reset był niedawno (" +
                           fmtDuration(now - r.lastAutoReset) + " temu). Nie resetuję drugi raz — decyzja należy do Ciebie.",
                           rescueButtons(false));
        } else {
          rescueDoReset("automatycznie");
        }
      }
      break;
    case R_HOLD:
      if (now >= r.holdUntil) { Lock l; gRescue.st = R_IDLE; }   // ask again next tick
      break;
    case R_RESET_DONE:
      if (now - r.resetAt >= RESET_GRACE_S) {
        { Lock l; gRescue.st = R_NEVER; }
        tgEnqueueStamped(simTag() + "❌ " + name + " nie wstał 15 min po resecie. Więcej nie próbuję — trzeba zajrzeć do serwera.",
                         rescueButtons(false));
      }
      break;
    case R_NEVER:
      break;
  }
}

const char* rescueStateName(RescueState s) {
  switch (s) {
    case R_NOTIFIED: return "zgłoszono awarię, czekam na decyzję";
    case R_HOLD: return "wstrzymane (Czekaj)";
    case R_NEVER: return "bez restartu przy tej awarii";
    case R_RESET_DONE: return "po resecie, czekam na powrót";
    default: return "spokój";
  }
}
