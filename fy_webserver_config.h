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

// Read credentials from compile-time defines
static void fyWsReadConfig(char *ssidBuf, size_t ssidLen,
                           char *passBuf, size_t passLen) {
  strncpy(ssidBuf, FY_WS_DEFAULT_SSID, ssidLen);
  strncpy(passBuf, FY_WS_DEFAULT_PASS, passLen);
}

#endif /* FY_WEBSERVER_CONFIG_H */
