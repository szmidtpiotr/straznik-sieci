#!/usr/bin/env python3
"""Panel preview without the device: serves firmware/src/ui_html.h with made-up sample data.

    python3 tools/panel_demo.py          # then open http://localhost:8000

Nothing is saved; buttons answer "ok" and change nothing. Used by panel_screenshots.py to
produce the pictures in docs/img/.
"""
import json
import random
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
HTML = re.search(r'R"HTML\((.*)\)HTML"', (ROOT / "firmware/src/ui_html.h").read_text(), re.S).group(1)
NOW = int(time.time())
MIN = 60

# id, name, type, host, extra fields, state, fails, ms, err, since (seconds ago)
TARGETS = [
    ("sek-siec", "Sieć", "separator", "", {}, "unknown", 0, 0, "", 0),
    ("router", "Router", "ping", "192.168.1.1", {}, "up", 0, 2, "", 9 * 86400),
    ("internet", "Internet (DNS 1.1.1.1)", "dns", "1.1.1.1", {"query": "api.telegram.org", "parent": "router"}, "up", 0, 14, "", 2 * 86400),
    ("switch", "Switch w szafce", "ping", "192.168.1.2", {"parent": "router"}, "up", 0, 1, "", 9 * 86400),
    ("ap-salon", "Wi-Fi salon", "ping", "192.168.1.3", {"parent": "switch"}, "up", 0, 3, "", 9 * 86400),
    ("sek-serwer", "Serwer", "separator", "", {}, "unknown", 0, 0, "", 0),
    ("serwer", "Serwer domowy", "tcp", "192.168.1.10", {"port": 8006, "parent": "switch"}, "up", 0, 4, "", 3 * 86400),
    ("ipmi", "IPMI serwera", "tcp", "192.168.1.9", {"port": 443, "parent": "switch"}, "up", 0, 6, "", 9 * 86400),
    ("ha", "Home Assistant", "http", "192.168.1.11", {"port": 8123, "path": "/manifest.json", "expect": 200, "parent": "serwer"}, "up", 0, 13, "", 3 * 86400),
    ("mqtt", "Broker MQTT", "tcp", "192.168.1.12", {"port": 1883, "parent": "serwer"}, "up", 0, 5, "", 3 * 86400),
    ("dns-lan", "AdGuard DNS", "dns", "192.168.1.13", {"query": "google.com", "parent": "serwer"}, "up", 1, 18, "brak odpowiedzi", 3 * 86400),
    ("nas", "NAS (zdjęcia)", "http", "192.168.1.20", {"port": 5000, "path": "/", "parent": "switch"}, "down", 7, 0, "connection refused", 14 * MIN),
    ("jellyfin", "Jellyfin", "tcp", "192.168.1.21", {"port": 8096, "parent": "nas"}, "down", 7, 0, "timeout", 14 * MIN),
    ("sek-dom", "Dom", "separator", "", {}, "unknown", 0, 0, "", 0),
    ("drukarka", "Drukarka 3D", "http", "192.168.1.30", {"port": 7125, "path": "/server/info", "expect": 200}, "up", 0, 21, "", 5 * 3600),
    ("kamera-wejscie", "Kamera wejście", "tcp", "192.168.1.31", {"port": 554}, "up", 0, 9, "", 86400),
    ("kamera-garaz", "Kamera garaż", "tcp", "192.168.1.32", {"port": 554, "muted_until": 2147000000}, "down", 30, 0, "timeout", 2 * 86400),
    ("brama", "Sterownik bramy", "ping", "192.168.1.33", {"enabled": False}, "unknown", 0, 0, "", 0),
    ("sek-www", "Strony w internecie", "separator", "", {}, "unknown", 0, 0, "", 0),
    ("www-blog", "blog.example.com", "http", "blog.example.com", {"port": 443, "tls": True, "every": 10, "needs": "internet", "parent": "serwer", "cert_days": 41, "full_ms": 1180}, "up", 0, 38, "", 86400),
]


def target_json(t):
    tid, name, typ, host, extra, state, fails, ms, err, ago = t
    d = {"id": tid, "name": name, "type": typ, "host": host, "path": "/", "query": "google.com", "parent": "",
         "enabled": True, "state": state, "fails": fails, "ms": ms, "err": err,
         "since": NOW - ago if ago else 0, "suppressed": False, "muted_until": 0}
    d.update(extra)
    d["suppressed"] = tid == "jellyfin"   # its parent (NAS) is down, so no separate alarm
    return d


def status():
    ts = [target_json(t) for t in TARGETS]
    real = [t for t in ts if t["type"] != "separator" and t["enabled"]]
    return {"targets": ts, "up": sum(t["state"] == "up" for t in real), "down": sum(t["state"] == "down" for t in real),
            "total": len(real), "muted_until": 0, "sweeps": 18234, "last_sweep": NOW - 12, "interval": 30,
            "fail_limit": 3, "heartbeat_hour": 8, "remind_min": 30,
            "rescue": {"target": "serwer", "hung_min": 5, "auto": False, "auto_min": 30, "win_start": "23:30",
                       "win_end": "07:00", "in_window": False, "ipmi_host": "192.168.1.9", "ipmi_user": "straznik",
                       "state": "spokój", "dry_run": True, "simulating": False, "power_checked": True, "power": "włączone"},
            "mqtt": {"host": "192.168.1.12", "port": 1883, "user": "straznik", "status": "połączone"},
            "ssid_cfg": "MojeWiFi", "tg": {"configured": True, "token_set": True, "chat": "123456789", "chat_set": True,
                                          "stage": "idle", "loop_age_ms": 240, "queue": 0, "error": "", "last_ok": NOW - 40,
                                          "pending_chat": "", "pending_name": ""},
            "probing": "", "fw": "0.9.4", "uptime": 3 * 86400 + 4 * 3600, "now": NOW, "ip": "192.168.1.50",
            "ssid": "MojeWiFi", "rssi": -52, "mac": "AA:BB:CC:DD:EE:FF", "heap": 201000, "psram": 8350000}


LOG = [
    (14 * MIN - 90, "DOWN: NAS (zdjęcia) — connection refused"),
    (5 * 3600, "UP: Drukarka 3D po 6 min"),
    (5 * 3600 + 400, "DOWN: Drukarka 3D — timeout"),
    (20 * 3600, "Wyciszono cel Kamera garaż z panelu do odwołania"),
    (3 * 86400 + 4 * 3600, "MQTT: połączono z 192.168.1.12"),
    (3 * 86400 + 4 * 3600 + 3, "Wi-Fi połączone, IP 192.168.1.50"),
    (3 * 86400 + 4 * 3600 + 5, "Start, firmware 0.9.4, przyczyna: aktualizacja OTA"),
]
TGLOG = [
    (14 * MIN - 90, "sent", "🔴 NAS (zdjęcia) nie odpowiada (connection refused) — zależnych od niego: 1"),
    (5 * 3600, "sent", "🟢 Drukarka 3D znów działa (przerwa 6 min)"),
    (5 * 3600 + 400, "sent", "🔴 Drukarka 3D nie odpowiada (timeout)"),
    (8 * 3600, "sent", "💚 Strażnik żyje (działa od 2 d 20 h)."),
    (20 * 3600, "in", "/wycisz kamera-garaz"),
]


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, body, ctype="application/json", code=200):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/":
            return self.send(HTML, "text/html")
        if u.path == "/api/status":
            return self.send(json.dumps(status(), ensure_ascii=False))
        if u.path == "/api/log":
            return self.send(json.dumps([{"t": NOW - a, "m": m} for a, m in LOG], ensure_ascii=False))
        if u.path == "/api/tglog":
            return self.send(json.dumps([{"t": NOW - a, "st": s, "text": x} for a, s, x in TGLOG], ensure_ascii=False))
        if u.path == "/api/targets":
            keep = ("id", "name", "type", "host", "port", "path", "query", "expect", "parent", "tls", "every", "needs")
            out = []
            for t in TARGETS:
                d = target_json(t)
                o = {k: d[k] for k in keep if k in d and d[k] not in ("", None) and not (k == "path" and d[k] == "/")
                     and not (k == "query" and d["type"] != "dns")}
                if not d["enabled"]:
                    o["enabled"] = False
                out.append(o)
            return self.send(json.dumps(out, ensure_ascii=False, indent=1))
        if u.path == "/api/target":
            tid = q.get("id", [""])[0]
            t = next((x for x in TARGETS if x[0] == tid), None)
            if not t:
                return self.send("nie ma celu", "text/plain", 404)
            rnd = random.Random(tid)
            base = max(t[7], 3)
            hist = [None if t[5] == "down" and i > 52 else round(base * rnd.uniform(0.7, 1.6)) for i in range(60)]
            return self.send(json.dumps({"id": tid, "name": t[1], "hist": hist}, ensure_ascii=False))
        self.send("nie ma", "text/plain", 404)

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(n)
        if u.path == "/api/ipmi":
            return self.send("włączone", "text/plain")
        self.send("ok", "text/plain")


if __name__ == "__main__":
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print(f"Podgląd panelu: http://localhost:{port}  (Ctrl+C kończy)")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
