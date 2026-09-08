// flock-you-esp32 — GPS waypoint support
// Records manual waypoints + appends GPS to detection JSON

#ifndef FY_GPS_H
#define FY_GPS_H

#include <Arduino.h>
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

// GPS globals (updated from I2C read task)
extern GpsFix gCurrentFix;

// Initialize GPS on I2C bus (Wire = SDA=21/SCL=22 on M5Stack Basic)
void gpsInit(TwoWire &bus = Wire, uint8_t sda = 21, uint8_t scl = 22);

// Read GPS once (non-blocking ~10ms). Returns true on valid fix.
bool gpsRead();

// Record a waypoint with current GPS position
bool waypointRecord(const char *label);

// Append GPS fields to detection JSON (called from drainAlertQueue)
void waypointAppendToJSON(char *buf, size_t len);

#endif /* FY_GPS_H */
