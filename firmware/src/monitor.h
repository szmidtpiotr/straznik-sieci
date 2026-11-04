// Monitor: sweeps all targets, keeps the UP/DOWN state machine and decides
// what goes to Telegram. A target whose parent is failing is not reported
// separately — the parent's message counts it instead.
#pragma once
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "state.h"
#include "probes.h"
#include "telegram.h"

volatile bool gSweepNow = false;
String gProbing;   // id of the target being probed right now (panel LED strip)
void runSweepNow() { gSweepNow = true; }
void rescueTick();   // rescue.h

// Parent failing (DOWN, or already missing answers) — children stay quiet.
static bool failingDep(const String& id, int depth);
// Something this target depends on (parent, or anything listed in `needs`) is failing.
static bool parentFailing(const Target& t, int depth = 0) {
  if (depth > 8) return false;
  if (t.parent.length() && failingDep(t.parent, depth)) return true;
  String rest = t.needs;
  while (rest.length()) {
    int c = rest.indexOf(','); String id = c < 0 ? rest : rest.substring(0, c); rest = c < 0 ? "" : rest.substring(c + 1);
    id.trim(); if (id.length() && failingDep(id, depth)) return true;
  }
  return false;
}
static bool failingDep(const String& id, int depth) {
  Target* p = findTarget(id);
  return p && monitored(*p) && (p->state == T_DOWN || p->fails > 0 || parentFailing(*p, depth + 1));
}

static bool hasAncestor(const Target& t, const String& id) {
  if (t.parent.isEmpty()) return false;
  if (t.parent == id) return true;
  Target* p = findTarget(t.parent);
  return p && hasAncestor(*p, id);
}

static int countDownBehind(const Target& t) {
  int n = 0;
  for (auto& c : gTargets) if (monitored(c) && c.state == T_DOWN && hasAncestor(c, t.id)) n++;
  return n;
}

String statusText(bool onlyProblems) {
  Lock l;
  int up = 0, down = 0, total = 0;
  String body;
  for (auto& t : gTargets) {
    if (t.type == P_SEP) { if (!onlyProblems) body += "\n— " + t.name + " —\n"; continue; }
    if (!monitored(t)) continue;
    total++;
    if (t.state == T_UP) up++;
    if (t.state == T_DOWN) down++;
    if (onlyProblems && t.state != T_DOWN && t.fails == 0) continue;
    if (targetMuted(t)) body += "🔕";
    if (t.state == T_DOWN) {
      body += (parentFailing(t) ? "⚫ " : "🔴 ") + t.name + " — od " + fmtTime(t.since) + " (" + t.lastErr + ")\n";
    } else if (t.fails > 0) {
      body += "🟡 " + t.name + " — " + t.fails + " nieudane próby (" + t.lastErr + ")\n";
    } else if (t.state == T_UP) {
      body += "🟢 " + t.name + " " + t.lastMs + " ms\n";
    } else {
      body += "⚪ " + t.name + " — jeszcze nie sprawdzony\n";
    }
  }
  String head = "Strażnik: " + String(up) + "/" + total + " OK";
  if (down) head += ", " + String(down) + " leży";
  if (isMuted()) head += "\n🔕 wyciszony " + fmtMute(gMutedUntil);
  int mutedTargets = 0;
  for (auto& t : gTargets) if (monitored(t) && targetMuted(t)) mutedTargets++;
  if (mutedTargets) head += "\n🔕 wyciszone cele: " + String(mutedTargets);
  int off = 0;
  for (auto& t : gTargets) if (!t.enabled && t.type != P_SEP) off++;
  if (off) head += "\n⏸ wyłączone z monitorowania: " + String(off);
  if (onlyProblems && body.isEmpty()) body = "Wszystko działa.\n";
  return head + "\n\n" + body;
}

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, r, g, b);
#endif
}

void updateLed() {
  if (!WiFi.isConnected()) { setLed(0, 0, 24); return; }
  Lock l;
  bool down = false, warn = false;
  for (auto& t : gTargets) if (monitored(t)) { down |= t.state == T_DOWN; warn |= t.fails > 0; }
  if (down) setLed(32, 0, 0); else if (warn) setLed(24, 12, 0); else setLed(0, 12, 0);
}

// Decides on messages after a full sweep and sends them as one Telegram message.
static void evaluateAlerts() {
  String msg, remind;
  std::vector<std::pair<String, String>> buttons;
  {
    Lock l;
    time_t now = time(nullptr);
    for (auto& t : gTargets) {
      if (!monitored(t)) continue;
      bool muted = targetMuted(t);
      if (t.state == T_DOWN && !t.alerted && !parentFailing(t) && !muted) {
        t.alerted = true;
        t.lastNotify = now;
        buttons.push_back({t.id, t.name});
        String line = "🔴 " + t.name + " nie odpowiada (" + t.lastErr + ")";
        int behind = countDownBehind(t);
        if (behind) line += " — zależnych od niego: " + String(behind);
        msg += line + "\n";
        logEvent("DOWN: " + t.name + " — " + t.lastErr);
      } else if (t.state == T_UP && t.alerted && muted) {
        t.alerted = false;                      // recovered while muted: close the outage quietly
        t.since = now;
      } else if (t.state == T_UP && t.alerted) {
        t.alerted = false;
        msg += "🟢 " + t.name + " znów działa (przerwa " + fmtDuration(now - t.since) + ")\n";
        t.since = now;
      } else if (t.state == T_DOWN && t.alerted && !muted && !parentFailing(t) && !isMuted() &&
                 gSet.remindMin && now - t.lastNotify >= (time_t)gSet.remindMin * 60) {
        // Still down and nobody muted it: remind. A global mute that just ended lands here too.
        t.lastNotify = now;
        buttons.push_back({t.id, t.name});
        remind += "• " + t.name + " — od " + fmtTime(t.since) + " (" + fmtDuration(now - t.since) + ")\n";
      }
    }
  }
  if (msg.isEmpty() && remind.isEmpty()) return;
  if (isMuted()) { if (msg.length()) logEvent("Wyciszony — nie wysyłam: " + msg); return; }
  if (remind.length()) msg += (msg.length() ? "\n" : "") + String("⏰ Nadal nie działa:\n") + remind;
  tgEnqueueStamped(msg, buttons.empty() ? "" : muteButtons(buttons));
}

static void applyResult(const String& id, bool ok, uint32_t ms, const String& err, uint8_t failLimit) {
  Lock l;
  Target* t = findTarget(id);
  if (!t) return;                               // list replaced during the sweep
  time_t now = time(nullptr);
  t->pushHist(ok, ms);
  if (ok) {
    if (t->state == T_DOWN) {
      logEvent("UP: " + t->name + " po " + fmtDuration(now - t->since));
      // An alerted outage keeps its start time: evaluateAlerts() reports the
      // duration and resets `since` afterwards.
      if (!t->alerted) t->since = now;
    } else if (t->state != T_UP) {
      t->since = now;
    }
    t->state = T_UP;
    t->fails = 0;
    t->lastMs = ms;
    t->lastErr = "";
  } else {
    t->lastErr = err;
    if (t->fails < 255) t->fails++;
    if (t->fails >= failLimit && t->state != T_DOWN) {
      t->state = T_DOWN;
      t->since = now;
    }
  }
}

void sweep() {
  // Probe a copy so the panel can replace the list mid-sweep; results are
  // matched back by id.
  std::vector<Target> snap;
  uint8_t failLimit;
  { Lock l; snap = gTargets; failLimit = gSet.failLimit; }
  uint32_t sweepNo; { Lock l; sweepNo = gSweeps; }
  for (auto& t : snap) {
    if (!monitored(t)) continue;
    if (t.every > 1) {                          // spread rare checks evenly over the sweeps
      uint32_t h = 0; for (char c : t.id) h = h * 31 + c;
      if ((sweepNo + h) % t.every) continue;
    }
    if (!WiFi.isConnected()) return;            // no verdicts without a network
    esp_task_wdt_reset();
    uint32_t ms = 0; String err;
    { Lock l; gProbing = t.id; }
    crumb(CR_MONITOR, ("cel " + t.id).c_str());
    ProbeExtra x;
    bool ok = runProbe(t, ms, err, x);
    { Lock l; gProbing = ""; Target* live = findTarget(t.id); if (live) { live->certDays = x.certDays; live->fullMs = x.fullMs; } }
    applyResult(t.id, ok, ms, err, failLimit);
  }
  { Lock l; gSweeps++; gLastSweep = time(nullptr); }
  evaluateAlerts();
  updateLed();
}

static void heartbeat() {
  static int lastDay = -1;
  int8_t hour; { Lock l; hour = gSet.heartbeatHour; }
  if (hour < 0 || !timeValid()) return;
  time_t now = time(nullptr);
  struct tm tm; localtime_r(&now, &tm);
  if (tm.tm_hour != hour || tm.tm_yday == lastDay) return;
  lastDay = tm.tm_yday;
  tgEnqueue("💚 Strażnik żyje (działa od " + fmtDuration(millis() / 1000) + ").\n" + statusText(true));
}

void monitorTask(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t last = 0;
  for (;;) {
    esp_task_wdt_reset();
    crumb(CR_MONITOR, "czeka");
    uint16_t interval; { Lock l; interval = gSet.interval; }
    bool due = last == 0 || millis() - last >= interval * 1000UL;
    if (WiFi.isConnected() && (due || gSweepNow)) {
      gSweepNow = false;
      last = millis();
      sweep();
      crumb(CR_MONITOR, "restart/IPMI");
      rescueTick();
      heartbeat();
    }
    if (!WiFi.isConnected()) updateLed();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
