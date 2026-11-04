#pragma once

#define FW_VERSION        "0.9.4"
#define HOSTNAME          "straznik"          // mDNS: straznik.local
#define SETUP_AP_SSID     "Straznik-Setup"

// Europe/Warsaw
#define TZ_INFO           "CET-1CEST,M3.5.0,M10.5.0/3"

// Resolvers used by the monitor itself. a local DNS (e.g. AdGuard) is usually a probed target,
// so the monitor must not depend on it.
#define DNS_PRIMARY       IPAddress(1, 1, 1, 1)
#define DNS_SECONDARY     IPAddress(8, 8, 8, 8)

// Defaults (overridable in the panel, stored in NVS).
#define DEF_INTERVAL_S    30      // seconds between sweeps
#define DEF_FAIL_LIMIT    3       // consecutive failures before DOWN
#define DEF_HEARTBEAT_H   8       // daily "I'm alive" hour, -1 = off
#define DEF_REMIND_MIN    30      // repeat "still down" every N minutes, 0 = off

#define PROBE_TIMEOUT_MS  3000
#define HTTP_TIMEOUT_MS   6000    // some apps (AI-GM health) take >1 s
#define WIFI_AP_AFTER_MS  60000   // start setup AP after this long without Wi-Fi
#define WIFI_ALERT_MS     120000  // report a Wi-Fi outage longer than this
#define WDT_TIMEOUT_S     60
#define TG_POLL_MS        10000
#define TG_QUEUE_MAX      40
#define LOG_SIZE          120
#define MAX_TARGETS       96
#define CERT_WARN_DAYS    14      // HTTPS certificate expiring sooner = failure
