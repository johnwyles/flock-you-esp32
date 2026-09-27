// flock-you-esp32 — M5Stack Basic tracking-mode screen
//
// Scope: draws the screen shown while GPS tracking mode (fy_track.h) is
// active. Runs on the UI task only (it touches M5Unified). Reads a
// FyTrackSnapshot and never touches the tracking state itself.
//
// Redraw strategy (same anti-flicker approach as m5basicScanning()): the body
// is cleared only on a full redraw (entering the screen, after an alert, or
// after a web-server screen). Otherwise lines are overwritten in place with
// fixed-width text on a solid background, once per second or when the
// snapshot version changes.

#pragma once
#if defined(USE_M5BASIC)

#include "fy_track.h"

static bool          mbt_full     = true;
static uint32_t      mbt_lastVer  = 0;
static unsigned long mbt_lastDraw = 0;

static void mbTrackForceRedraw() { mbt_full = true; }

static void mbt_line(int y, uint16_t fg, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void mbt_line(int y, uint16_t fg, const char *fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // pad to full width so shorter text overwrites the previous value
    size_t n = strlen(buf);
    while (n < 51) buf[n++] = ' ';
    buf[n] = '\0';
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(fg, MB_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.print(buf);
}

static void m5basicTracking(const FyTrackSnapshot &s, uint8_t ch, int detCount) {
    // A detection alert is on screen: leave it for the normal hold time
    // (C short press clears the hold), then come back with a full redraw.
    if (mb_lastAlertMs != 0 && (millis() - mb_lastAlertMs) < MB_ALERT_HOLD_MS) {
        mbt_full = true;
        return;
    }
    if (mb_needsRedraw) { mbt_full = true; mb_needsRedraw = false; }
    bool changed = (s.version != mbt_lastVer);
    if (!mbt_full && !changed && millis() - mbt_lastDraw < 1000) return;
    mbt_lastVer = s.version;
    mbt_lastDraw = millis();

    char hdrR[28];
    snprintf(hdrR, sizeof(hdrR), "Ch:%-2u  Det:%-3d", (unsigned)ch, detCount);
    mb_header("REC  TRACKING", hdrR, MB_DARK_RED, MB_WHITE);

    if (mbt_full) {
        M5.Display.fillRect(0, MB_HDR_H, MB_W, MB_BTN_Y - MB_HDR_H, MB_BLACK);
        mb_hline(108);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(MB_GREY, MB_BLACK);
        M5.Display.setCursor(8, 114);
        M5.Display.print("Recent points (newest first):");
        mb_btnBar("SAVE", "WPT/STOP", "WEB");
        mbt_full = false;
    }

    // Big elapsed timer + interval
    char el[12];
    snprintf(el, sizeof(el), "%02lu:%02lu:%02lu", (unsigned long)(s.elapsedS / 3600),
             (unsigned long)((s.elapsedS / 60) % 60), (unsigned long)(s.elapsedS % 60));
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(MB_RED, MB_BLACK);
    M5.Display.setCursor(8, 24);
    M5.Display.printf("REC %s", el);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MB_LT_GREY, MB_BLACK);
    M5.Display.setCursor(200, 30);
    M5.Display.printf("every %-4lus", (unsigned long)s.intervalS);

    mbt_line(48, MB_LT_GREY, "File: %s", s.path);
    mbt_line(60, MB_WHITE, "Points: %-6lu  Skipped (no fix): %lu",
             (unsigned long)s.points, (unsigned long)s.skipped);
    if (s.fix) {
        mbt_line(72, MB_GREEN, "GPS: FIX  %u used / %u in view  HDOP %.1f",
                 s.satsUsed, s.satsInView, s.hdop);
        mbt_line(84, MB_WHITE, "Lat/Lon: %.6f, %.6f", s.lat, s.lon);
        mbt_line(96, MB_WHITE, "Alt: %.1f m   Speed: %.1f km/h", s.alt, s.spd);
    } else {
        mbt_line(72, MB_YELLOW, "GPS: NO FIX  %u used / %u in view  waiting...",
                 s.satsUsed, s.satsInView);
        mbt_line(84, MB_GREY, "Lat/Lon: --");
        mbt_line(96, MB_GREY, "Alt: --   Speed: --");
    }

    int y = 126;
    for (int i = 0; i < FY_TRACK_RECENT; i++, y += 12) {
        if (i < s.recentCount) {
            const FyTrackPoint &p = s.recent[i];
            mbt_line(y, p.mark ? MB_CYAN : MB_LT_GREY, "#%-4lu%s %10.5f,%11.5f %5.1fkm/h %2usat",
                     (unsigned long)p.seq, p.mark ? "*" : " ", p.lat, p.lon, p.spd, p.sats);
        } else {
            mbt_line(y, MB_GREY, i == 0 ? "(none yet)" : "");
        }
    }
    // Feedback line for A/B presses while tracking (shown for 8 s)
    mbt_line(200, MB_CYAN, "%s", (s.note[0] && s.noteAgeS < 8) ? s.note : "");
}

#endif  // USE_M5BASIC
