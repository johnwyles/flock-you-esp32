// flock-you-esp32 — Optional-module diagnostics (GPS + CC1101) for M5Stack Basic
//
// Scope: work out WHETHER the stacked M5Stack GPS Module v2.1 (AT6668 +
// MAX2659, UART) and Module CC1101 (SPI) are physically attached and
// answering, and on WHICH pins (both modules route their signals through
// DIP switches, so the pins are not fixed). Results are:
//   - printed to the serial console at boot and on CMD:DIAG,
//   - summarised on the device screen log at boot,
//   - served by the web server at /modules (JSON) and on the / page.
// Nothing here runs during normal scanning; it only probes at boot or when
// asked via CMD:DIAG.

#ifndef FY_MODULE_DIAG_H
#define FY_MODULE_DIAG_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

struct FyGpsDiag {
  bool     probed;
  bool     found;
  uint8_t  transport;        // GpsTransport (fy_gps.h)
  int8_t   rxPin;            // ESP32 RX pin the NMEA stream arrived on
  uint32_t baud;
  uint8_t  i2cAddr;
  uint32_t probeMs;
  char     i2cDevices[96];   // every address that ACKed on Wire (G21/G22)
  char     sample[84];       // first valid NMEA sentence seen
  char     tried[320];       // per-pin/baud results, human readable
  char     hint[256];
};

struct FyCc1101Diag {
  bool     probed;
  bool     found;
  int8_t   csPin;
  int8_t   gdo0Pin;          // -1 = not identified
  int8_t   gdo2Pin;
  uint8_t  partnum;
  uint8_t  version;
  uint8_t  marcstate;
  char     tried[320];       // per-CS results, human readable
  char     hint[256];
};

extern FyGpsDiag    gGpsDiag;
extern FyCc1101Diag gCc1101Diag;

// Probe the GPS (UART candidates, then I2C). Blocking: ~1.3 s per silent pin, capped at ~9 s.
bool fyDiagProbeGps();

// Probe the CC1101 on every CSn pin its DIP switch can select.
// sdInUse: skip legacy G4 (SD card CS). loraPresent: skip G5 as a GDO pin.
bool fyDiagProbeCc1101(bool sdInUse, bool loraPresent);

// Human-readable report (Serial, WiFiClient, ...).
void fyDiagPrint(Print &out);

// HTML card for the web UI root page.
void fyDiagPrintHtml(Print &out);

// JSON object for /modules. Returns bytes written.
size_t fyDiagJson(char *buf, size_t len);

// Map an ESP32 GPIO to its M-Bus pin label, e.g. "G15/MBus23".
const char *fyDiagPinLabel(int8_t gpio);

#endif /* FY_MODULE_DIAG_H */
