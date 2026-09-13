// fy_globals.h — Centralized extern declarations for all shared globals
//
// Every module includes this to get extern declarations for variables defined
// in main.cpp.  This replaces ad-hoc `extern` blocks scattered across files.
#pragma once

#include <Arduino.h>
#include <stdint.h>

// ── Persistence constants (must match main.cpp) ─────────────────────────────
#ifndef MAX_DETECTIONS
#define MAX_DETECTIONS 200
#endif

// ── Module presence flags (defined in main.cpp) ─────────────────────────────
extern bool gHasGPS;
extern bool gHasLoRa;
extern bool gHasCC1101;

// ── Storage state (defined in main.cpp) ───────────────────────────────────────
extern bool fySpiffsReady;

// ── Web server mode toggle (defined in main.cpp, used by fy_serial.cpp) ───────
extern bool gWebServerMode;

// ── Debug level (defined in main.cpp, used by fy_serial.cpp) ───────────────────
extern int gDebugLevel;

// ── Detection table (defined in main.cpp) ─────────────────────────────────────
extern int fyDetCount;
extern bool fyDirty;

// ── Channel / scan state (defined in main.cpp) ──────────────────────────────
extern uint8_t currentChannel;
extern bool channelLockActive;

// ── WiFi sniffer + channel helpers (defined in main.cpp) ─────────────────────
extern const char *channelModeName();
extern void applyInitialChannel();
extern bool gSdRawReady;

// ── Alert queue (defined in main.cpp) ───────────────────────────────────────────
// AlertType enum — defined here with a guard so main.cpp (which has the
// original definition) can skip its own if it includes fy_globals.h.
#ifndef ALERT_TYPE_DEFINED
#define ALERT_TYPE_DEFINED
enum AlertType : uint8_t
{
  ALERT_OUI_ADDR2 = 0,
  ALERT_OUI_ADDR1 = 1,
  ALERT_OUI_ADDR3 = 2,
  ALERT_SSID = 3,
  ALERT_WILDCARD_PROBE = 4,
  ALERT_LAA_SSID = 5,
  ALERT_OUI_MFR = 6,
  ALERT_SOUNDTHINKING = 7,
  ALERT_BLE_MFR_ID = 8,
  ALERT_BLE_RAVEN_UUID = 9,
  ALERT_BLE_NAME = 10,
};
#endif

enum AlertType;
extern void enqueueAlert(AlertType type, const uint8_t *mac, int8_t rssi,
                          uint8_t ch, const char *ssid, const char *kind,
                          uint8_t confidence);

// ── Web server (defined in fy_webserver.cpp) ───────────────────────────────────
extern bool fyWebServerActive();

// ── Storage / session persistence (defined in main.cpp) ────────────────────────
extern void fySaveSession();

// ── BLE debugging (defined in main.cpp when BLE enabled) ─────────────────────
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
extern void bleInjectFake();
#endif