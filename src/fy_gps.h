// flock-you-esp32 — GPS waypoint support
// Records manual waypoints + appends GPS to detection JSON.
//
// Transports:
//   UART — M5Stack GPS Module v2.1 (AT6668 + MAX2659, M-Bus stacking module,
//          115200 8N1, TX/RX pins chosen by the module's DIP switches) and
//          GPS Unit v1.1 on Port C (G16/G17). This is the normal case.
//   I2C  — legacy path for I2C-capable GNSS receivers (0x10 / 0x42).
// fy_module_diag.cpp works out which transport/pin/baud the receiver is on.

#ifndef FY_GPS_H
#define FY_GPS_H

#include <Arduino.h>
#include <Wire.h>
#include "storage_backend.h"

#define MAX_WAYPOINTS 32
#define WAYPOINT_FILE "/waypoints.json"

struct GpsFix {
  double lat;
  double lon;
  float  alt;
  float  speed;
  uint8_t satellites;
  float  hdop;
  bool   valid;
  unsigned long timestampMs;
};

struct Waypoint {
  char      label[32];
  GpsFix    fix;
};

enum GpsTransport : uint8_t { GPS_TRANSPORT_NONE = 0, GPS_TRANSPORT_I2C = 1, GPS_TRANSPORT_UART = 2 };

// Live receiver statistics — used by the diagnostics page / CMD:GPS.
struct GpsStats {
  uint8_t  transport;        // GpsTransport
  int8_t   rxPin;            // ESP32 RX pin (UART) or -1
  uint32_t baud;             // UART baud or 0
  uint8_t  i2cAddr;          // I2C address or 0
  uint32_t bytes;            // raw bytes received
  uint32_t sentences;        // NMEA sentences with a valid checksum
  uint32_t checksumErrors;   // sentences that failed the checksum
  uint32_t gga, rmc, gsv;    // per-type counters
  uint8_t  fixQuality;       // GGA field 6 (0 = no fix)
  uint8_t  satsInView;       // from GSV
  unsigned long lastSentenceMs;
  char     lastSentence[84]; // most recent valid sentence (truncated)
};

// GPS globals
extern GpsFix gCurrentFix;
extern GpsStats gGpsStats;

// Module presence flags (defined in main.cpp)
extern bool gHasGPS;
extern bool gHasLoRa;
extern bool gHasCC1101;

// Initialize GPS on I2C bus (Wire = SDA=21/SCL=22 on M5Stack Basic)
void gpsInit(TwoWire &bus = Wire, uint8_t sda = 21, uint8_t scl = 22, uint8_t addr = 0x10);

// Initialize GPS on a UART (M5Stack GPS Module v2.1 / GPS Unit on Port C).
// txPin may be -1 (we only listen; the receiver streams NMEA on its own).
void gpsInitUart(HardwareSerial &port, int8_t rxPin, int8_t txPin, uint32_t baud);

// Read GPS once (non-blocking). Returns true when a new valid fix was parsed.
bool gpsRead();

// Feed one raw byte to the NMEA parser (exposed for the diagnostics probe).
// Returns true when that byte completed a checksum-valid sentence.
bool gpsFeedChar(char c);

// True once the system clock has been set from a valid GPS RMC sentence
// (the M5Stack Basic has no battery-backed RTC, so time() starts at 1970).
bool gpsClockValid();

// Record a waypoint with current GPS position
bool waypointRecord(const char *label);

// Append GPS fields to detection JSON (called from drainAlertQueue)
void waypointAppendToJSON(char *buf, size_t len);
bool waypointRecordManual(const char *label);

#endif /* FY_GPS_H */
