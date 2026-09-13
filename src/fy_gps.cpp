// flock-you-esp32 — GPS waypoint support
// Supports M5Stack GPS Unit v1.1 (AT6668) and similar I2C GPS modules

#include "fy_gps.h"
#include <Wire.h>
#include <time.h>

void gpsInit(TwoWire &bus, uint8_t sda, uint8_t scl) {
  bus.begin(sda, scl);
  gCurrentFix.valid = false;
  Serial.printf("[gps] init on I2C SDA=%d SCL=%d\n", sda, scl);
}

bool waypointRecord(const char *label) {
  return waypointRecordManual(label);
}

// GPS I2C address
static const uint8_t GPS_ADDR = 0x10;

// Current fix state
GpsFix gCurrentFix;

// NMEA parser state
static char nmeaBuf[128];
static uint8_t nmeaIdx = 0;

static bool gpsReadNMEA() {
  if (!gHasGPS) return false;
  Wire.beginTransmission(GPS_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  int avail = Wire.requestFrom((int)GPS_ADDR, 64);
  if (avail <= 0) return false;
  while (avail-- > 0 && Wire.available()) {
    char c = Wire.read();
    if (c == '\n') {
      nmeaBuf[nmeaIdx] = '\0';
      nmeaIdx = 0;
      if (strncmp(nmeaBuf, "$GPGGA", 6) == 0 || strncmp(nmeaBuf, "$GNGGA", 6) == 0) {
        char *p = nmeaBuf;
        char *tokens[16];
        int ti = 0;
        tokens[ti++] = strtok_r(p, ",", &p);
        while (ti < 16 && tokens[ti-1]) tokens[ti++] = strtok_r(NULL, ",", &p);
        if (ti > 9 && tokens[2][0] && tokens[4][0]) {
          gCurrentFix.valid = true;
          gCurrentFix.lat = atof(tokens[2]) / 100.0;
          if (tokens[3][0] == 'S') gCurrentFix.lat = -gCurrentFix.lat;
          gCurrentFix.lon = atof(tokens[4]) / 100.0;
          if (tokens[6][0] == 'W') gCurrentFix.lon = -gCurrentFix.lon;
          gCurrentFix.alt = tokens[9][0] ? atof(tokens[9]) : 0.0;
          gCurrentFix.satellites = tokens[8][0] ? atoi(tokens[8]) : 0;
          gCurrentFix.hdop = tokens[10][0] ? atof(tokens[10]) : 99.9;
          return true;
        }
      }
    } else if (c != '\r') {
      if (nmeaIdx < sizeof(nmeaBuf) - 1) nmeaBuf[nmeaIdx++] = c;
    }
  }
  return false;
}

bool gpsRead() {
  if (!gHasGPS) return false;
  return gpsReadNMEA();
}

void waypointAppendToJSON(char *buf, size_t len) {
  if (!gCurrentFix.valid) {
    snprintf(buf, len, ",\"gps\":null");
    return;
  }
  snprintf(buf, len, ",\"gps\":{\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,"
                     "\"sat\":%d,\"hdop\":%.1f}",
           gCurrentFix.lat, gCurrentFix.lon, gCurrentFix.alt,
           gCurrentFix.satellites, gCurrentFix.hdop);
}

static bool waypointWriteFile(const char *path, const char *entry) {
  File f = fyOpen(path, "a");
  if (!f) return false;
  f.print(entry);
  f.close();
  return true;
}

static void waypointRollDate(char *path, size_t len) {
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  snprintf(path, len, "/waypoints-%04d-%02d-%02d.json",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

bool waypointRecordManual(const char *label) {
  if (!gCurrentFix.valid) {
    Serial.println("[gps] manual waypoint skipped: no fix");
    return false;
  }
  char path[64];
  waypointRollDate(path, sizeof(path));
  char entry[160];
  snprintf(entry, sizeof(entry),
           "{\"ts\":%lu,\"label\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,"
           "\"alt\":%.1f,\"sat\":%d,\"hdop\":%.1f}\n",
           (unsigned long)time(nullptr), label,
           gCurrentFix.lat, gCurrentFix.lon, gCurrentFix.alt,
           gCurrentFix.satellites, gCurrentFix.hdop);
  if (waypointWriteFile(path, entry)) {
    Serial.printf("[gps] waypoint saved: %s (%.6f, %.6f)\n", label, gCurrentFix.lat, gCurrentFix.lon);
    return true;
  }
  Serial.println("[gps] waypoint save FAILED");
  return false;
}
