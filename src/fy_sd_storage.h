// flock-you-esp32 — SD card raw file storage for M5Launcher
// Reads/writes flock_you-NNNN.json directly to SD:/firmwares/flock-you/data/

#ifndef FY_SD_STORAGE_H
#define FY_SD_STORAGE_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// SD card pins for M5Stack Basic Port A (HSPI)
// MOSI=23, MISO=19, SCK=18, CS=4
static const int SD_CS = 4;
static const int SD_MOSI = 23;
static const int SD_MISO = 19;
static const int SD_SCK = 18;

static bool sdInit() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI)) return false;
  return true;
}

static File sdOpenNext(const char *prefix, const char *ext, char *outPath, size_t outLen) {
  // Scan for next sequential filename: prefix-NNNN.ext
  int maxIdx = -1;
  File root = SD.open("/");
  if (!root) return File();
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String name = f.name();
      // Match prefix-NNNN.ext
      int dash = name.lastIndexOf('-');
      int dot = name.lastIndexOf('.');
      if (dash > 0 && dot > dash) {
        String base = name.substring(0, dash);
        String num = name.substring(dash + 1, dot);
        String extn = name.substring(dot + 1);
        if (base.equalsIgnoreCase(prefix) && extn.equalsIgnoreCase(ext)) {
          int idx = num.toInt();
          if (idx > maxIdx) maxIdx = idx;
        }
      }
    }
    f = root.openNextFile();
  }
  root.close();

  int next = maxIdx + 1;
  snprintf(outPath, outLen, "/%s-%04d.%s", prefix, next, ext);
  return SD.open(outPath, FILE_WRITE);
}

static bool sdWriteJSON(const char *json) {
  char path[64];
  File f = sdOpenNext("flock_you", "json", path, sizeof(path));
  if (!f) return false;
  f.print(json);
  f.close();
  return true;
}

static String sdListJSON() {
  String out = "[";
  File root = SD.open("/");
  if (!root) return out + "]";
  File f = root.openNextFile();
  bool first = true;
  while (f) {
    if (!f.isDirectory()) {
      String name = f.name();
      if (!first) out += ",";
      out += "\"" + name + "\"";
      first = false;
    }
    f = root.openNextFile();
  }
  root.close();
  return out + "]";
}

#endif /* FY_SD_STORAGE_H */
