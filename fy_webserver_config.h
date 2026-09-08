// flock-you-esp32 — Webserver configuration
// Edit these or set via config.json on ESP32 filesystem

#ifndef FY_WEBSERVER_CONFIG_H
#define FY_WEBSERVER_CONFIG_H

#include <Arduino.h>

// Default AP credentials (overridden by config.json if present)
#ifdef FY_WS_SSID
#define FY_WS_DEFAULT_SSID FY_WS_SSID
#else
#define FY_WS_DEFAULT_SSID "flock-you"
#endif

#ifdef FY_WS_PASS
#define FY_WS_DEFAULT_PASS FY_WS_PASS
#else
#define FY_WS_DEFAULT_PASS "flockyou"
#endif
#define FY_WS_IP         IPAddress(192, 168, 4, 1)
#define FY_WS_GATEWAY    IPAddress(192, 168, 4, 1)
#define FY_WS_SUBNET     IPAddress(255, 255, 255, 0)
#define FY_WS_PORT       80

// Read credentials from config.json if present, else use defaults
static bool fyWsReadConfig(char *ssidBuf, size_t ssidLen,
                           char *passBuf, size_t passLen) {
  // Try config.json first
  File f = SPIFFS.open("/config.json", "r");
  if (f) {
    size_t sz = f.size();
    if (sz < 512) {
      String json = f.readString();
      f.close();
      // Parse SSID
      int ssidIdx = json.indexOf("\"ssid\"");
      if (ssidIdx >= 0) {
        int start = json.indexOf(':', ssidIdx) + 1;
        int end = json.indexOf(',', start);
        if (end < 0) end = json.indexOf('}', start);
        String val = json.substring(start, end);
        val.trim();
        val.replace("\"", "");
        val.toCharArray(ssidBuf, ssidLen);
      }
      // Parse password
      int passIdx = json.indexOf("\"password\"");
      if (passIdx >= 0) {
        int start = json.indexOf(':', passIdx) + 1;
        int end = json.indexOf(',', start);
        if (end < 0) end = json.indexOf('}', start);
        String val = json.substring(start, end);
        val.trim();
        val.replace("\"", "");
        val.toCharArray(passBuf, passLen);
      }
      return true;
    }
    f.close();
  }
  // Fallback to defaults
  strncpy(ssidBuf, FY_WS_DEFAULT_SSID, ssidLen);
  strncpy(passBuf, FY_WS_DEFAULT_PASS, passLen);
  return false;
}

#endif /* FY_WEBSERVER_CONFIG_H */
