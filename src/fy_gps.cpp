// flock-you-esp32 — GPS waypoint support
// UART: M5Stack GPS Module v2.1 (AT6668 + MAX2659) / GPS Unit v1.1 on Port C
// I2C:  legacy I2C GNSS receivers (0x10 / 0x42)

#include "fy_gps.h"
#include <Wire.h>
#include <time.h>

// Current fix state + receiver statistics
GpsFix gCurrentFix;
GpsStats gGpsStats;

static TwoWire *gGpsWire = nullptr;
static HardwareSerial *gGpsUart = nullptr;

// NMEA parser state
static char nmeaBuf[100];
static uint8_t nmeaIdx = 0;

void gpsInit(TwoWire &bus, uint8_t sda, uint8_t scl, uint8_t addr) {
  bus.begin(sda, scl);
  gGpsWire = &bus;
  gGpsUart = nullptr;
  gCurrentFix.valid = false;
  gGpsStats.transport = GPS_TRANSPORT_I2C;
  gGpsStats.i2cAddr = addr;
  gGpsStats.rxPin = -1;
  gGpsStats.baud = 0;
  Serial.printf("[gps] init on I2C SDA=%d SCL=%d addr=0x%02X\n", sda, scl, addr);
}

void gpsInitUart(HardwareSerial &port, int8_t rxPin, int8_t txPin, uint32_t baud) {
  port.end();
  port.setRxBufferSize(1024);  // a 1 Hz AT6668 burst is ~600 bytes
  port.begin(baud, SERIAL_8N1, rxPin, txPin);
  gGpsUart = &port;
  gGpsWire = nullptr;
  gCurrentFix.valid = false;
  gGpsStats.transport = GPS_TRANSPORT_UART;
  gGpsStats.rxPin = rxPin;
  gGpsStats.baud = baud;
  gGpsStats.i2cAddr = 0;
  Serial.printf("[gps] init on UART RX=G%d TX=%s%d @ %lu baud\n", rxPin,
                txPin >= 0 ? "G" : "", txPin, (unsigned long)baud);
}

bool waypointRecord(const char *label) {
  return waypointRecordManual(label);
}

// Split an NMEA sentence in place on ',' and '*', PRESERVING empty fields
// (strtok collapses ",,", which shifted every field on no-fix sentences).
static int nmeaSplit(char *s, char **fields, int maxFields) {
  int n = 0;
  fields[n++] = s;
  for (char *p = s; *p && n < maxFields; p++) {
    if (*p == ',' || *p == '*') {
      bool star = (*p == '*');
      *p = '\0';
      if (star) break;
      fields[n++] = p + 1;
    }
  }
  return n;
}

static bool nmeaChecksumOk(const char *s) {
  if (s[0] != '$') return false;
  const char *star = strchr(s, '*');
  if (!star || strlen(star) < 3) return false;
  uint8_t sum = 0;
  for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
  return sum == (uint8_t)strtoul(star + 1, nullptr, 16);
}

// ddmm.mmmm (or dddmm.mmmm) -> signed decimal degrees
static double nmeaToDegrees(const char *v, const char *hemi) {
  if (!v[0]) return 0.0;
  double raw = atof(v);
  int deg = (int)(raw / 100.0);
  double deg_d = deg + (raw - deg * 100.0) / 60.0;
  if (hemi[0] == 'S' || hemi[0] == 'W') deg_d = -deg_d;
  return deg_d;
}

// Returns true if this sentence produced a valid fix.
static bool nmeaProcess(char *line) {
  if (!nmeaChecksumOk(line)) {
    gGpsStats.checksumErrors++;
    return false;
  }
  gGpsStats.sentences++;
  gGpsStats.lastSentenceMs = millis();
  strncpy(gGpsStats.lastSentence, line, sizeof(gGpsStats.lastSentence) - 1);
  gGpsStats.lastSentence[sizeof(gGpsStats.lastSentence) - 1] = '\0';

  if (strlen(line) < 6) return false;
  const char *type = line + 3;  // skip "$GN"/"$GP"/"$BD"/"$GB" talker
  char *f[20];
  int n = nmeaSplit(line, f, 20);

  if (strncmp(type, "GGA", 3) == 0 && n >= 10) {
    // $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,...
    gGpsStats.gga++;
    gGpsStats.fixQuality = (uint8_t)atoi(f[6]);
    gCurrentFix.satellites = (uint8_t)atoi(f[7]);
    gCurrentFix.hdop = f[8][0] ? atof(f[8]) : 99.9f;
    if (gGpsStats.fixQuality > 0 && f[2][0] && f[4][0]) {
      gCurrentFix.lat = nmeaToDegrees(f[2], f[3]);
      gCurrentFix.lon = nmeaToDegrees(f[4], f[5]);
      gCurrentFix.alt = f[9][0] ? atof(f[9]) : 0.0f;
      gCurrentFix.valid = true;
      gCurrentFix.timestampMs = millis();
      return true;
    }
    gCurrentFix.valid = false;
  } else if (strncmp(type, "RMC", 3) == 0 && n >= 8) {
    // $xxRMC,time,status,lat,N,lon,E,speed_kn,...
    gGpsStats.rmc++;
    if (f[2][0] == 'A') gCurrentFix.speed = atof(f[7]) * 1.852f;  // km/h
  } else if (strncmp(type, "GSV", 3) == 0 && n >= 4) {
    gGpsStats.gsv++;
    gGpsStats.satsInView = (uint8_t)atoi(f[3]);
  }
  return false;
}

bool gpsFeedChar(char c) {
  gGpsStats.bytes++;
  if (c == '$') nmeaIdx = 0;  // resync on every sentence start
  if (c == '\n' || c == '\r') {
    if (nmeaIdx == 0) return false;
    nmeaBuf[nmeaIdx] = '\0';
    nmeaIdx = 0;
    uint32_t before = gGpsStats.sentences;
    nmeaProcess(nmeaBuf);
    return gGpsStats.sentences != before;
  }
  if (nmeaIdx < sizeof(nmeaBuf) - 1) nmeaBuf[nmeaIdx++] = c;
  return false;
}

static bool gpsReadI2C() {
  if (!gGpsWire) return false;
  uint8_t addr = gGpsStats.i2cAddr ? gGpsStats.i2cAddr : 0x10;
  int avail = gGpsWire->requestFrom((int)addr, 64);
  if (avail <= 0) return false;
  uint32_t before = gGpsStats.gga;
  while (gGpsWire->available()) {
    char c = gGpsWire->read();
    if (c == (char)0xFF) continue;  // idle filler when the buffer is empty
    gpsFeedChar(c);
  }
  return (gGpsStats.gga != before) && gCurrentFix.valid;
}

static bool gpsReadUart() {
  if (!gGpsUart) return false;
  uint32_t before = gGpsStats.gga;
  int budget = 1024;
  while (budget-- > 0 && gGpsUart->available()) gpsFeedChar((char)gGpsUart->read());
  return (gGpsStats.gga != before) && gCurrentFix.valid;
}

bool gpsRead() {
  if (!gHasGPS) return false;
  if (gGpsStats.transport == GPS_TRANSPORT_UART) return gpsReadUart();
  if (gGpsStats.transport == GPS_TRANSPORT_I2C) return gpsReadI2C();
  return false;
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
