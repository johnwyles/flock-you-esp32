// flock-you-esp32 — GPS track logging ("tracking mode"). See fy_track.h.

#include "fy_track.h"
#include "fy_gps.h"
#include "storage_backend.h"
#include <time.h>
#include <freertos/FreeRTOS.h>

extern bool gHasGPS;
extern bool fySpiffsReady;

static portMUX_TYPE sTrackMux = portMUX_INITIALIZER_UNLOCKED;
static FyTrackSnapshot sSnap;          // guarded by sTrackMux
static uint32_t sIntervalS = FY_TRACK_DEFAULT_INTERVAL_S;
static unsigned long sStartMs = 0;
static unsigned long sLastTickMs = 0;
static unsigned long sNoteMs = 0;
static uint32_t sSeq = 0;
static uint32_t sPoints = 0;
static uint32_t sSkipped = 0;
static uint32_t sWriteErrors = 0;
static char sPath[24] = {0};
static bool sActive = false;

bool fyTrackActive() { return sActive; }
uint32_t fyTrackInterval() { return sIntervalS; }

void fyTrackSetInterval(uint32_t seconds) {
  if (seconds < 1) seconds = 1;
  if (seconds > 3600) seconds = 3600;
  sIntervalS = seconds;
  portENTER_CRITICAL(&sTrackMux);
  sSnap.intervalS = seconds;
  sSnap.version++;
  portEXIT_CRITICAL(&sTrackMux);
  Serial.printf("[track] interval set to %lus\r\n", (unsigned long)seconds);
}

void fyTrackSetNote(const char *msg) {
  sNoteMs = millis();
  portENTER_CRITICAL(&sTrackMux);
  strncpy(sSnap.note, msg, sizeof(sSnap.note) - 1);
  sSnap.note[sizeof(sSnap.note) - 1] = '\0';
  sSnap.version++;
  portEXIT_CRITICAL(&sTrackMux);
}

static void fmtUtc(char *buf, size_t len, time_t t) {
  if (!gpsClockValid()) { snprintf(buf, len, "unset"); return; }
  struct tm tm;
  gmtime_r(&t, &tm);
  snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
           tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static bool nextTrackPath(char *out, size_t len) {
  for (int n = 1; n <= 9999; n++) {
    snprintf(out, len, "/track-%04d.json", n);
    if (!fyExists(out)) return true;
  }
  return false;
}

// Append one line to the track file. Opens/closes the file per line so a
// power cut loses at most the line being written. kind: 0 = interval point,
// 1 = manual mark, 2 = interval tick with no fix (position fields null).
static bool appendLine(const char *line, int n) {
  File f = fyOpen(sPath, "a");
  if (!f) {
    Serial.printf("[track] WRITE FAILED: cannot open %s\r\n", sPath);
    return false;
  }
  size_t w = f.write((const uint8_t *)line, n);
  f.close();
  if ((int)w != n) {
    Serial.printf("[track] WRITE FAILED: short write (%u/%d) to %s\r\n", (unsigned)w, n, sPath);
    return false;
  }
  return true;
}

static bool writeLine(int kind) {
  time_t now = time(nullptr);
  char utc[24];
  fmtUtc(utc, sizeof(utc), now);
  char pos[160];
  if (kind == 2) {
    snprintf(pos, sizeof(pos),
             "\"lat\":null,\"lon\":null,\"alt\":null,\"spd_kmh\":null,\"sats\":%u,"
             "\"sats_view\":%u,\"hdop\":null",
             gCurrentFix.satellites, gGpsStats.satsInView);
  } else {
    snprintf(pos, sizeof(pos),
             "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"spd_kmh\":%.1f,\"sats\":%u,"
             "\"sats_view\":%u,\"hdop\":%.1f",
             gCurrentFix.lat, gCurrentFix.lon, gCurrentFix.alt, gCurrentFix.speed,
             gCurrentFix.satellites, gGpsStats.satsInView, gCurrentFix.hdop);
  }
  const char *ev = kind == 0 ? "pt" : kind == 1 ? "mark" : "nofix";
  char line[256];
  int n = snprintf(line, sizeof(line),
                   "{\"seq\":%lu,\"event\":\"%s\",\"utc\":\"%s\",\"ts\":%lu,\"up_ms\":%lu,%s}\n",
                   (unsigned long)(sSeq + 1), ev, utc,
                   gpsClockValid() ? (unsigned long)now : 0UL, (unsigned long)millis(), pos);
  if (!appendLine(line, n)) return false;
  sSeq++;

  if (kind == 2) {
    Serial.printf("[track] #%lu nofix (%u used / %u in view) -> %s\r\n", (unsigned long)sSeq,
                  gCurrentFix.satellites, gGpsStats.satsInView, sPath);
    return true;
  }
  Serial.printf("[track] #%lu%s %.6f,%.6f alt=%.0fm spd=%.1fkm/h sats=%u hdop=%.1f\r\n",
                (unsigned long)sSeq, kind == 1 ? " MARK" : "", gCurrentFix.lat, gCurrentFix.lon,
                gCurrentFix.alt, gCurrentFix.speed, gCurrentFix.satellites, gCurrentFix.hdop);
  FyTrackPoint p;
  p.lat = gCurrentFix.lat;
  p.lon = gCurrentFix.lon;
  p.spd = gCurrentFix.speed;
  p.sats = gCurrentFix.satellites;
  p.seq = sSeq;
  p.mark = (kind == 1);
  portENTER_CRITICAL(&sTrackMux);
  for (int i = FY_TRACK_RECENT - 1; i > 0; i--) sSnap.recent[i] = sSnap.recent[i - 1];
  sSnap.recent[0] = p;
  if (sSnap.recentCount < FY_TRACK_RECENT) sSnap.recentCount++;
  sSnap.version++;
  portEXIT_CRITICAL(&sTrackMux);
  return true;
}

bool fyTrackStart() {
  if (sActive) return true;
  if (!gHasGPS) {
    Serial.print("[track] cannot start: no GPS module detected\r\n");
    return false;
  }
  if (!fySpiffsReady) {
    Serial.print("[track] cannot start: storage not ready\r\n");
    return false;
  }
  if (!nextTrackPath(sPath, sizeof(sPath))) {
    Serial.print("[track] cannot start: no free /track-NNNN.json name\r\n");
    return false;
  }
  sStartMs = millis();
  sLastTickMs = 0;  // first point on the next tick
  sSeq = sPoints = sSkipped = sWriteErrors = 0;
  sActive = true;
  portENTER_CRITICAL(&sTrackMux);
  uint32_t ver = sSnap.version;
  memset(&sSnap, 0, sizeof(sSnap));
  sSnap.active = true;
  sSnap.intervalS = sIntervalS;
  strncpy(sSnap.path, sPath, sizeof(sSnap.path) - 1);
  sSnap.version = ver + 1;
  portEXIT_CRITICAL(&sTrackMux);
  Serial.printf("[track] START interval=%lus file=%s\r\n", (unsigned long)sIntervalS, sPath);
  fyTrackSetNote("Tracking started");
  return true;
}

void fyTrackStop() {
  if (!sActive) return;
  sActive = false;
  uint32_t el = (millis() - sStartMs) / 1000;
  Serial.printf("[track] STOP %lu points with fix, %lu no-fix ticks, %lu write errors, "
                "%lum%02lus, %lu lines in %s\r\n",
                (unsigned long)sPoints, (unsigned long)sSkipped, (unsigned long)sWriteErrors,
                (unsigned long)(el / 60), (unsigned long)(el % 60), (unsigned long)sSeq, sPath);
  portENTER_CRITICAL(&sTrackMux);
  sSnap.active = false;
  sSnap.version++;
  portEXIT_CRITICAL(&sTrackMux);
}

bool fyTrackMark() {
  if (!sActive || !gCurrentFix.valid) return false;
  return writeLine(1);
}

void fyTrackTick() {
  if (!sActive) return;
  unsigned long now = millis();

  if (sLastTickMs == 0 || now - sLastTickMs >= sIntervalS * 1000UL) {
    sLastTickMs = now;
    if (gCurrentFix.valid) {
      if (writeLine(0)) sPoints++;
      else sWriteErrors++;
    } else {
      // Still write a line, so the file proves recording was running and
      // shows what the receiver saw (satellites in view) while waiting.
      sSkipped++;
      if (!writeLine(2)) sWriteErrors++;
    }
  }

  // Refresh the live part of the snapshot (cheap; UI redraws on version change
  // or its own 1 s timer).
  static unsigned long lastSnapMs = 0;
  if (now - lastSnapMs < 250) return;
  lastSnapMs = now;
  portENTER_CRITICAL(&sTrackMux);
  sSnap.elapsedS = (now - sStartMs) / 1000;
  sSnap.points = sPoints;
  sSnap.skipped = sSkipped;
  sSnap.writeErrors = sWriteErrors;
  sSnap.fix = gCurrentFix.valid;
  sSnap.satsUsed = gCurrentFix.satellites;
  sSnap.satsInView = gGpsStats.satsInView;
  sSnap.hdop = gCurrentFix.hdop;
  sSnap.lat = gCurrentFix.lat;
  sSnap.lon = gCurrentFix.lon;
  sSnap.alt = gCurrentFix.alt;
  sSnap.spd = gCurrentFix.speed;
  sSnap.noteAgeS = sNoteMs ? (now - sNoteMs) / 1000 : 9999;
  portEXIT_CRITICAL(&sTrackMux);
}

void fyTrackGetSnapshot(FyTrackSnapshot &out) {
  portENTER_CRITICAL(&sTrackMux);
  out = sSnap;
  portEXIT_CRITICAL(&sTrackMux);
}
