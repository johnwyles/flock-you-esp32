// flock-you-esp32 — Webserver configuration
// WiFi credentials injected at compile time from .env via build_flags

#ifndef FY_WEBSERVER_CONFIG_H
#define FY_WEBSERVER_CONFIG_H

#include <Arduino.h>

// Default AP credentials (overridden by .env/build_flags at compile time)
#define FY_WS_DEFAULT_SSID "flock-you"
#define FY_WS_DEFAULT_PASS "flockyou"
#define FY_WS_IP         IPAddress(192, 168, 4, 1)
#define FY_WS_GATEWAY    IPAddress(192, 168, 4, 1)
#define FY_WS_SUBNET     IPAddress(255, 255, 255, 0)
#define FY_WS_PORT       80

// Read credentials — prefers compile-time defines from .env (via generate_build_flags.py)
// Falls back to hardcoded defaults. Ensures password is at least 8 chars (WPA2 minimum).
static void fyWsReadConfig(char *ssidBuf, size_t ssidLen,
                           char *passBuf, size_t passLen) {
#ifdef FY_WS_SSID
  strncpy(ssidBuf, FY_WS_SSID, ssidLen);
#else
  strncpy(ssidBuf, FY_WS_DEFAULT_SSID, ssidLen);
#endif
  ssidBuf[ssidLen - 1] = '\0';

#ifdef FY_WS_PASS
  strncpy(passBuf, FY_WS_PASS, passLen);
  // If compile-time password is too short (< 8 chars), fall back to default
  if (strlen(passBuf) < 8) {
    strncpy(passBuf, FY_WS_DEFAULT_PASS, passLen);
  }
#else
  strncpy(passBuf, FY_WS_DEFAULT_PASS, passLen);
#endif
  passBuf[passLen - 1] = '\0';
}

#endif /* FY_WEBSERVER_CONFIG_H */
