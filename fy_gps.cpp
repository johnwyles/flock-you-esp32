// flock-you-esp32 — GPS waypoint support
// Supports M5Stack GPS Unit v1.1 (AT6668) and similar I2C GPS modules

#include "fy_gps.h"
#include <Wire.h>

static GpsFix gFix;
static bool gpsEnabled = false;

// NMEA sentence buffer
static char nmeaBuf[256];
static int nmeaLen = 0;

// Parse GPGGA sentence for position data
static bool parseGPGGA(const char *sentence) {
  // $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,fix,nsat,hdop,alt,M,...
  const char *p = sentence;
  // Skip "$GPGGA,"
  if (strncmp(p, "$GPGGA,", 7) != 0) return false;
  p += 7;

  // Parse UTC time (skip)
  while (*p && *p != ',') p++;
  if (*p != ',') return false;
  p++;

  // Parse latitude
  double lat = 0, lon = 0;
  char latDir = 'N', lonDir = 'E';
  int fixQuality = 0;
  int satellites = 0;
  float hdop = 99.9, alt = 0.0;

  // Latitude: ddmm.mmmm
  if (*p && *p != ',') {
    lat = atof(p);
    int deg = (int)(lat / 100);
    double mins = lat - (deg * 100);
    lat = deg + mins / 60.0;
    while (*p && *p != ',') p++;
    if (*p == ',') { p++; latDir = *p; p++; }
    while (*p && *p != ',') p++;
  }
  if (*p == ',') p++;

  // Longitude: dddmm.mmmm
  if (*p && *p != ',') {
    lon = atof(p);
    int deg = (int)(lon / 100);
    double mins = lon - (deg * 100);
    lon = deg + mins / 60.0;
    while (*p && *p != ',') p++;
    if (*p == ',') { p++; lonDir = *p; p++; }
    while (*p && *p != ',') p++;
  }
  if (*p == ',') p++;

  // Fix quality
  if (*p && *p != ',') {
    fixQuality = *p - '0';
    while (*p && *p != ',') p++;
  }
  if (*p == ',') p++;

  // Satellites
  if (*p && *p != ',') {
    satellites = atoi(p);
    while (*p && *p != ',') p++;
  }
  if (*p == ',') p++;

  // HDOP
  if (*p && *p != ',') {
    hdop = atof(p);
    while (*p && *p != ',') p++;
  }
  if (*p == ',') p++;

  // Altitude
  if (*p && *p != ',') {
    alt = atof(p);
    while (*p && *p != ',') p++;
  }

  if (fixQuality > 0 && satellites > 0) {
    if (latDir == 'S') lat = -lat;
    if (lonDir == 'W') lon = -lon;
    gFix.lat = lat;
    gFix.lon = lon;
    gFix.alt = alt;
    gFix.satellites = satellites;
    gFix.hdop = hdop;
    gFix.valid = true;
    gFix.timestampMs = millis();
    return true;
  }
  return false;
}

// Try to read NMEA bytes from I2C GPS module
static bool i2cReadNMEA() {
  // M5Stack GPS Unit v1.1 (AT6668): read NMEA from register 0x10
  Wire.beginTransmission(0x10);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;

  // Read up to 64 bytes
  int avail = Wire.requestFrom(0x10, 64);
  if (avail < 10) return false;

  int idx = 0;
  while (Wire.available() && idx < 250) {
    char c = Wire.read();
    if (c == '\n') {
      nmeaBuf[nmeaLen] = 0;
      if (nmeaLen > 6 && strncmp(nmeaBuf, "$GPGGA", 6) == 0) {
        return parseGPGGA(nmeaBuf);
      }
      nmeaLen = 0;
    } else if (c >= 0x20 && c < 0x7F && nmeaLen < 255) {
      nmeaBuf[nmeaLen++] = c;
    }
  }
  return false;
}

void gpsInit(TwoWire &bus, uint8_t sda, uint8_t scl) {
  bus.begin(sda, scl);
  delay(50);
  gpsEnabled = true;
  gFix.valid = false;
  nmeaLen = 0;
}

bool gpsRead() {
  if (!gpsEnabled) return false;
  return i2cReadNMEA();
}

bool waypointRecord(const char *label) {
  if (!gFix.valid) {
    Serial.println("[gps] no fix — waypoint not recorded");
    return false;
  }

  // Load existing waypoints
  File f = fyOpen(WAYPOINT_FILE, "r");
  int count = 0;
  if (f) {
    // Count existing entries
    while (f.available()) {
      if (f.read() == '{') count++;
    }
    f.close();
  }

  if (count >= MAX_WAYPOINTS) {
    Serial.println("[gps] waypoint limit reached");
    return false;
  }

  // Append new waypoint
  f = fyOpen(WAYPOINT_FILE, "a");
  if (!f) {
    Serial.println("[gps] waypoint save failed");
    return false;
  }

  unsigned long ts = gFix.timestampMs;
  f.printf("{\"label\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,"
           "\"sat\":%d,\"hdop\":%.1f,\"ts\":%lu}\n",
           label, gFix.lat, gFix.lon, gFix.alt,
           gFix.satellites, gFix.hdop, ts);
  f.close();
  Serial.printf("[gps] waypoint saved: %s (%.6f, %.6f)\n", label, gFix.lat, gFix.lon);
  return true;
}

void waypointAppendToJSON(char *buf, size_t len) {
  if (!gFix.valid) {
    snprintf(buf, len, ",\"gps\":null");
    return;
  }
  snprintf(buf, len, ",\"gps\":{\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,"
                     "\"sat\":%d,\"hdop\":%.1f}",
           gFix.lat, gFix.lon, gFix.alt, gFix.satellites, gFix.hdop);
}

GpsFix gCurrentFix;
