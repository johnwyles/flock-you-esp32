// flock-you-esp32 — GPS track logging ("tracking mode")
//
// Scope: interval-based GPS point logging to a per-session file, started and
// stopped by holding Btn B (or CMD:TRACK). Owns the session state, the file
// writes, and a thread-safe snapshot the UI task reads to draw the tracking
// screen. It does NOT draw anything and does NOT touch M5Unified.
//
// Threading: fyTrackStart/Stop/Tick/Mark/SetNote run on the loop() task (the
// only task that writes storage). fyTrackGetSnapshot() may be called from
// the UI task.
//
// File: /track-NNNN.json on the active storage (SD or SPIFFS), one JSON
// object per line, every line with the same keys so the web /table view can
// render it:
//   {"seq":1,"event":"pt","utc":"2026-09-27T20:05:27Z","ts":1790539527,
//    "up_ms":123456,"lat":30.073551,"lon":-97.842667,"alt":210.0,
//    "spd_kmh":3.2,"sats":9,"sats_view":14,"hdop":0.8}
// event = "pt" (interval point), "mark" (manual Btn B press while tracking)
// or "nofix" (interval tick without a fix: position fields are null, sats and
// sats_view still recorded, so the file always shows the tracker was running).

#ifndef FY_TRACK_H
#define FY_TRACK_H

#include <Arduino.h>

#define FY_TRACK_DEFAULT_INTERVAL_S 5
#define FY_TRACK_RECENT             6

struct FyTrackPoint {
  double   lat;
  double   lon;
  float    spd;
  uint8_t  sats;
  uint32_t seq;
  bool     mark;
};

struct FyTrackSnapshot {
  bool     active;
  uint32_t intervalS;
  uint32_t elapsedS;
  uint32_t points;
  uint32_t skipped;       // ticks with no fix (written as "nofix" lines)
  uint32_t writeErrors;   // lines that failed to write
  char     path[24];
  // live GPS
  bool     fix;
  uint8_t  satsUsed;
  uint8_t  satsInView;
  float    hdop;
  double   lat, lon;
  float    alt, spd;
  // newest first
  uint8_t      recentCount;
  FyTrackPoint recent[FY_TRACK_RECENT];
  char     note[48];      // last status message (waypoint/save feedback)
  uint32_t noteAgeS;
  uint32_t version;       // bumps whenever something visible changed
};

bool fyTrackActive();
bool fyTrackStart();             // false if no GPS / storage / file error
void fyTrackStop();
void fyTrackTick();              // call every loop() iteration
void fyTrackSetInterval(uint32_t seconds);
uint32_t fyTrackInterval();
// Record the current fix as a "mark" line in the track file (Btn B short
// press while tracking). Returns false if not tracking or no fix.
bool fyTrackMark();
// Short status line shown on the tracking screen (e.g. "Waypoint saved").
void fyTrackSetNote(const char *msg);
void fyTrackGetSnapshot(FyTrackSnapshot &out);

#endif /* FY_TRACK_H */
